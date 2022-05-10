/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "system_ability_manager.h"

#include <cinttypes>
#include <unistd.h>

#include "ability_death_recipient.h"
#include "datetime_ex.h"
#include "directory_ex.h"
#include "errors.h"
#include "if_local_ability_manager.h"
#include "ipc_skeleton.h"
#include "local_ability_manager_proxy.h"
#include "parse_util.h"
#include "sam_log.h"
#include "service_control.h"
#include "string_ex.h"
#include "system_ability_definition.h"
#include "tools.h"

using namespace std;

namespace OHOS {
namespace {
const string PREFIX = "/system/profile/";
constexpr int32_t MAX_NAME_SIZE = 200;
constexpr int32_t SPLIT_NAME_VECTOR_SIZE = 2;

constexpr int32_t UID_ROOT = 0;
constexpr int32_t UID_SYSTEM = 1000;
constexpr int32_t MAX_SUBSCRIBE_COUNT = 256;
constexpr int64_t CHECK_LOADED_DELAY_TIME = 60 * 1000;
}

std::mutex SystemAbilityManager::instanceLock;
sptr<SystemAbilityManager> SystemAbilityManager::instance;

SystemAbilityManager::SystemAbilityManager()
{
    dBinderService_ = DBinderService::GetInstance();
}

SystemAbilityManager::~SystemAbilityManager()
{
}

void SystemAbilityManager::Init()
{
    abilityDeath_ = sptr<IRemoteObject::DeathRecipient>(new AbilityDeathRecipient());
    systemProcessDeath_ = sptr<IRemoteObject::DeathRecipient>(new SystemProcessDeathRecipient());
    abilityStatusDeath_ = sptr<IRemoteObject::DeathRecipient>(new AbilityStatusDeathRecipient());
    abilityCallbackDeath_ = sptr<IRemoteObject::DeathRecipient>(new AbilityCallbackDeathRecipient());
    rpcCallbackImp_ = make_shared<RpcCallbackImp>();
    if (workHandler_ == nullptr) {
        auto runner = AppExecFwk::EventRunner::Create("workHandler");
        workHandler_ = make_shared<AppExecFwk::EventHandler>(runner);
    }
    InitSaProfile();
}

const sptr<DBinderService> SystemAbilityManager::GetDBinder() const
{
    return dBinderService_;
}

sptr<SystemAbilityManager> SystemAbilityManager::GetInstance()
{
    std::lock_guard<std::mutex> autoLock(instanceLock);
    if (instance == nullptr) {
        instance = new SystemAbilityManager;
    }
    return instance;
}

void SystemAbilityManager::InitSaProfile()
{
    if (workHandler_ == nullptr) {
        HILOGE("InitSaProfile parseHandler_ not init!");
        return;
    }

    auto callback = [this] () {
        int64_t begin = GetTickCount();
        std::vector<std::string> fileNames;
        GetDirFiles(PREFIX, fileNames);
        auto parser = std::make_shared<ParseUtil>();
        for (const auto& file : fileNames) {
            if (file.empty() || file.find(".xml") == std::string::npos
                || file.find("_trust.xml") != std::string::npos) {
                continue;
            }
            parser->ParseSaProfiles(file);
        }
        auto saInfos = parser->GetAllSaProfiles();
        lock_guard<mutex> autoLock(saProfileMapLock_);
        for (const auto& saInfo : saInfos) {
            saProfileMap_[saInfo.saId] = saInfo;
        }
        HILOGI("[PerformanceTest] InitSaProfile spend %{public}" PRId64 " ms", GetTickCount() - begin);
    };
    bool ret = workHandler_->PostTask(callback);
    if (!ret) {
        HILOGW("SystemAbilityManager::InitSaProfile PostTask fail");
    }
}

bool SystemAbilityManager::GetSaProfile(int32_t saId, SaProfile& saProfile)
{
    lock_guard<mutex> autoLock(saProfileMapLock_);
    auto iter = saProfileMap_.find(saId);
    if (iter == saProfileMap_.end()) {
        return false;
    } else {
        saProfile = iter->second;
    }
    return true;
}

sptr<IRemoteObject> SystemAbilityManager::GetSystemAbility(int32_t systemAbilityId)
{
    return CheckSystemAbility(systemAbilityId);
}

sptr<IRemoteObject> SystemAbilityManager::GetSystemAbility(int32_t systemAbilityId, const std::string& deviceId)
{
    return CheckSystemAbility(systemAbilityId, deviceId);
}

sptr<IRemoteObject> SystemAbilityManager::GetSystemAbilityFromRemote(int32_t systemAbilityId)
{
    HILOGD("%{public}s called, systemAbilityId = %{public}d", __func__, systemAbilityId);
    if (!CheckInputSysAbilityId(systemAbilityId)) {
        HILOGW("GetSystemAbilityFromRemote invalid!");
        return nullptr;
    }

    shared_lock<shared_mutex> readLock(abilityMapLock_);
    auto iter = abilityMap_.find(systemAbilityId);
    if (iter == abilityMap_.end()) {
        HILOGI("GetSystemAbilityFromRemote not found service : %{public}d.", systemAbilityId);
        return nullptr;
    }
    if (!(iter->second.isDistributed)) {
        HILOGW("GetSystemAbilityFromRemote service : %{public}d not distributed", systemAbilityId);
        return nullptr;
    }
    HILOGI("GetSystemAbilityFromRemote found service : %{public}d.", systemAbilityId);
    return iter->second.remoteObj;
}

sptr<IRemoteObject> SystemAbilityManager::CheckSystemAbility(int32_t systemAbilityId)
{
    HILOGD("%{public}s called, systemAbilityId = %{public}d", __func__, systemAbilityId);
    if (!CheckInputSysAbilityId(systemAbilityId)) {
        HILOGW("CheckSystemAbility CheckSystemAbility invalid!");
        return nullptr;
    }

    shared_lock<shared_mutex> readLock(abilityMapLock_);
    auto iter = abilityMap_.find(systemAbilityId);
    if (iter != abilityMap_.end()) {
        HILOGI("found service : %{public}d.", systemAbilityId);
        return iter->second.remoteObj;
    }
    HILOGW("NOT found service : %{public}d", systemAbilityId);
    return nullptr;
}

bool SystemAbilityManager::CheckDistributedPermission()
{
    auto callingUid = IPCSkeleton::GetCallingUid();
    if (callingUid != UID_ROOT && callingUid != UID_SYSTEM) {
        return false;
    }
    return true;
}

sptr<IRemoteObject> SystemAbilityManager::CheckSystemAbility(int32_t systemAbilityId,
    const std::string& deviceId)
{
    sptr<DBinderServiceStub> remoteBinder = nullptr;
    if (dBinderService_ != nullptr) {
        string strName = to_string(systemAbilityId);
        remoteBinder = dBinderService_->MakeRemoteBinder(Str8ToStr16(strName), deviceId, systemAbilityId, 0);
        HILOGI("CheckSystemAbility, MakeRemoteBinder, systemAbilityId is %{public}d, deviceId is %s",
            systemAbilityId, deviceId.c_str());
        if (remoteBinder == nullptr) {
            HILOGE("MakeRemoteBinder error, remoteBinder is null");
        }
    }
    return remoteBinder;
}

int32_t SystemAbilityManager::FindSystemAbilityNotify(int32_t systemAbilityId, int32_t code)
{
    return FindSystemAbilityNotify(systemAbilityId, "", code);
}

void SystemAbilityManager::NotifySystemAbilityChanged(int32_t systemAbilityId, const std::string& deviceId,
    int32_t code, const sptr<ISystemAbilityStatusChange>& listener)
{
    if (listener == nullptr) {
        HILOGE("%s listener null pointer!", __func__);
        return;
    }

    switch (code) {
        case ADD_SYSTEM_ABILITY_TRANSACTION: {
            listener->OnAddSystemAbility(systemAbilityId, deviceId);
            break;
        }
        case REMOVE_SYSTEM_ABILITY_TRANSACTION: {
            listener->OnRemoveSystemAbility(systemAbilityId, deviceId);
            break;
        }
        default:
            break;
    }
}

int32_t SystemAbilityManager::FindSystemAbilityNotify(int32_t systemAbilityId, const std::string& deviceId,
    int32_t code)
{
    HILOGI("%s called:systemAbilityId = %{public}d, code = %{public}d", __func__, systemAbilityId, code);
    lock_guard<recursive_mutex> autoLock(listenerMapLock_);
    auto iter = listenerMap_.find(systemAbilityId);
    if (iter != listenerMap_.end()) {
        auto& listeners = iter->second;
        for (const auto& item : listeners) {
            NotifySystemAbilityChanged(systemAbilityId, deviceId, code, item.first);
        }
    }

    return ERR_OK;
}

bool SystemAbilityManager::IsNameInValid(const std::u16string& name)
{
    HILOGI("%{public}s called:name = %{public}s", __func__, Str16ToStr8(name).c_str());
    bool ret = false;
    if (name.empty() || name.size() > MAX_NAME_SIZE || DeleteBlank(name).empty()) {
        ret = true;
    }

    return ret;
}

void SystemAbilityManager::StartOnDemandAbility(const std::u16string& procName, int32_t systemAbilityId)
{
    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    auto iter = startingAbilityMap_.find(systemAbilityId);
    if (iter == startingAbilityMap_.end()) {
        return;
    }
    auto& abilityItem = iter->second;
    StartOnDemandAbilityInner(procName, systemAbilityId, abilityItem);
}

void SystemAbilityManager::StartOnDemandAbilityInner(const std::u16string& procName, int32_t systemAbilityId,
    AbilityItem& abilityItem)
{
    if (abilityItem.state != AbilityState::INIT) {
        return;
    }
    sptr<ILocalAbilityManager> procObject =
        iface_cast<ILocalAbilityManager>(GetSystemProcess(procName));
    if (procObject == nullptr) {
        HILOGI("get process:%{public}s fail", Str16ToStr8(procName).c_str());
        return;
    }
    procObject->StartAbility(systemAbilityId);
    abilityItem.state = AbilityState::STARTING;
}

int32_t SystemAbilityManager::AddOnDemandSystemAbilityInfo(int32_t systemAbilityId,
    const std::u16string& procName)
{
    HILOGI("%{public}s called", __func__);
    if (!CheckInputSysAbilityId(systemAbilityId) || IsNameInValid(procName)) {
        HILOGW("AddOnDemandSystemAbilityInfo systemAbilityId or procName invalid.");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    auto onDemandSaSize = onDemandAbilityMap_.size();
    if (onDemandSaSize >= MAX_SERVICES) {
        HILOGE("map size error, (Has been greater than %{public}zu)",
            onDemandAbilityMap_.size());
        return ERR_INVALID_VALUE;
    }

    if (systemProcessMap_.count(procName) == 0) {
        HILOGW("AddOnDemandSystemAbilityInfo procName:%{public}s not exist.", Str16ToStr8(procName).c_str());
        return ERR_INVALID_VALUE;
    }
    onDemandAbilityMap_[systemAbilityId] = procName;
    HILOGI("insert onDemand systemAbilityId:%{public}d. size : %{public}zu", systemAbilityId,
        onDemandAbilityMap_.size());
    if (startingAbilityMap_.count(systemAbilityId) != 0) {
        if (workHandler_ != nullptr) {
            auto pendingTask = [procName, systemAbilityId, this] () {
                StartOnDemandAbility(procName, systemAbilityId);
            };
            bool ret = workHandler_->PostTask(pendingTask);
            if (!ret) {
                HILOGW("AddOnDemandSystemAbilityInfo PostTask failed!");
            }
        }
    }
    return ERR_OK;
}

int32_t SystemAbilityManager::StartOnDemandAbility(int32_t systemAbilityId)
{
    lock_guard<recursive_mutex> onDemandAbilityLock(onDemandLock_);
    auto iter = onDemandAbilityMap_.find(systemAbilityId);
    if (iter == onDemandAbilityMap_.end()) {
        return ERR_INVALID_VALUE;
    }
    HILOGI("found onDemandAbility: %{public}d.", systemAbilityId);
    AbilityItem& abilityItem = startingAbilityMap_[systemAbilityId];
    StartOnDemandAbilityInner(iter->second, systemAbilityId, abilityItem);
    return ERR_OK;
}

sptr<IRemoteObject> SystemAbilityManager::CheckSystemAbility(int32_t systemAbilityId, bool& isExist)
{
    if (!CheckInputSysAbilityId(systemAbilityId)) {
        return nullptr;
    }
    sptr<IRemoteObject> abilityProxy = CheckSystemAbility(systemAbilityId);
    if (abilityProxy == nullptr) {
        lock_guard<recursive_mutex> autoLock(onDemandLock_);
        auto iter = startingAbilityMap_.find(systemAbilityId);
        if (iter != startingAbilityMap_.end() && iter->second.state == AbilityState::STARTING) {
            isExist = true;
            return nullptr;
        }

        int32_t ret = StartOnDemandAbility(systemAbilityId);
        if (ret == ERR_OK) {
            isExist = true;
            return nullptr;
        }

        isExist = false;
        return nullptr;
    }

    isExist = true;
    return abilityProxy;
}

int32_t SystemAbilityManager::RemoveSystemAbility(int32_t systemAbilityId)
{
    HILOGI("%s called (name)", __func__);
    if (!CheckInputSysAbilityId(systemAbilityId)) {
        HILOGW("RemoveSystemAbility systemAbilityId:%{public}d", systemAbilityId);
        return ERR_INVALID_VALUE;
    }
    {
        unique_lock<shared_mutex> writeLock(abilityMapLock_);
        auto itSystemAbility = abilityMap_.find(systemAbilityId);
        if (itSystemAbility == abilityMap_.end()) {
            HILOGI("SystemAbilityManager::RemoveSystemAbility not found!");
            return ERR_INVALID_VALUE;
        }
        sptr<IRemoteObject> ability = itSystemAbility->second.remoteObj;
        if (ability != nullptr && abilityDeath_ != nullptr) {
            ability->RemoveDeathRecipient(abilityDeath_);
        }
        (void)abilityMap_.erase(itSystemAbility);
        HILOGI("%s called, systemAbilityId : %{public}d, size : %{public}zu", __func__, systemAbilityId,
            abilityMap_.size());
    }
    SendSystemAbilityRemovedMsg(systemAbilityId);
    return ERR_OK;
}

int32_t SystemAbilityManager::RemoveSystemAbility(const sptr<IRemoteObject>& ability)
{
    HILOGI("%s called, (ability)", __func__);
    if (ability == nullptr) {
        HILOGW("ability is nullptr ");
        return ERR_INVALID_VALUE;
    }

    int32_t saId = 0;
    {
        unique_lock<shared_mutex> writeLock(abilityMapLock_);
        for (auto iter = abilityMap_.begin(); iter != abilityMap_.end(); ++iter) {
            if (iter->second.remoteObj == ability) {
                saId = iter->first;
                (void)abilityMap_.erase(iter);
                if (abilityDeath_ != nullptr) {
                    ability->RemoveDeathRecipient(abilityDeath_);
                }
                HILOGI("%s called, systemAbilityId:%{public}d removed, size : %{public}zu", __func__, saId,
                    abilityMap_.size());
                break;
            }
        }
    }

    if (saId != 0) {
        SendSystemAbilityRemovedMsg(saId);
    }
    return ERR_OK;
}

vector<u16string> SystemAbilityManager::ListSystemAbilities(uint32_t dumpFlags)
{
    vector<u16string> list;
    shared_lock<shared_mutex> readLock(abilityMapLock_);
    for (auto iter = abilityMap_.begin(); iter != abilityMap_.end(); iter++) {
        list.emplace_back(Str8ToStr16(to_string(iter->first)));
    }
    return list;
}

int32_t SystemAbilityManager::SubscribeSystemAbility(int32_t systemAbilityId,
    const sptr<ISystemAbilityStatusChange>& listener)
{
    HILOGI("%s called", __func__);
    if (!CheckInputSysAbilityId(systemAbilityId) || listener == nullptr) {
        HILOGW("SubscribeSystemAbility systemAbilityId or listener invalid!");
        return ERR_INVALID_VALUE;
    }

    auto callingPid = IPCSkeleton::GetCallingPid();
    {
        lock_guard<recursive_mutex> autoLock(listenerMapLock_);
        auto& listeners = listenerMap_[systemAbilityId];
        for (const auto& itemListener : listeners) {
            if (listener->AsObject() == itemListener.first->AsObject()) {
                HILOGI("already exist listener object systemAbilityId = %{public}d", systemAbilityId);
                return ERR_OK;
            }
        }
        auto& count = subscribeCountMap_[callingPid];
        if (count >= MAX_SUBSCRIBE_COUNT) {
            HILOGE("SubscribeSystemAbility pid:%{public}d overflow max subscribe count!", callingPid);
            return ERR_PERMISSION_DENIED;
        }
        ++count;
        if (abilityStatusDeath_ != nullptr) {
            bool ret = listener->AsObject()->AddDeathRecipient(abilityStatusDeath_);
            listeners.emplace_back(listener, callingPid);
            HILOGI("SubscribeSystemAbility systemAbilityId = %{public}d AddDeathRecipient %{public}s",
                systemAbilityId, ret ? "succeed" : "failed");
        }
        HILOGI("SubscribeSystemAbility systemAbilityId = %{public}d, size = %{public}zu", systemAbilityId,
            listeners.size());
    }
    sptr<IRemoteObject> targetObject = CheckSystemAbility(systemAbilityId);
    if (targetObject != nullptr) {
        NotifySystemAbilityChanged(systemAbilityId, "", ADD_SYSTEM_ABILITY_TRANSACTION, listener);
    }
    return ERR_OK;
}

void SystemAbilityManager::UnSubscribeSystemAbilityLocked(
    std::list<std::pair<sptr<ISystemAbilityStatusChange>, int32_t>>& listenerList,
    const sptr<IRemoteObject>& listener)
{
    auto iter = listenerList.begin();
    while (iter != listenerList.end()) {
        auto& item = *iter;
        if (item.first->AsObject() != listener) {
            ++iter;
            continue;
        }

        if (abilityStatusDeath_ != nullptr) {
            listener->RemoveDeathRecipient(abilityStatusDeath_);
        }
        auto iterPair = subscribeCountMap_.find(item.second);
        if (iterPair != subscribeCountMap_.end()) {
            --iterPair->second;
            if (iterPair->second == 0) {
                subscribeCountMap_.erase(iterPair);
            }
        }
        iter = listenerList.erase(iter);
        break;
    }
}

int32_t SystemAbilityManager::UnSubscribeSystemAbility(int32_t systemAbilityId,
    const sptr<ISystemAbilityStatusChange>& listener)
{
    HILOGI("%s called", __func__);
    if (!CheckInputSysAbilityId(systemAbilityId) || listener == nullptr) {
        HILOGW("UnSubscribeSystemAbility systemAbilityId or listener invalid!");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(listenerMapLock_);
    auto& listeners = listenerMap_[systemAbilityId];
    UnSubscribeSystemAbilityLocked(listeners, listener->AsObject());
    HILOGI("UnSubscribeSystemAbility systemAbilityId = %{public}d, size = %{public}zu", systemAbilityId,
        listeners.size());
    return ERR_OK;
}

void SystemAbilityManager::UnSubscribeSystemAbility(const sptr<IRemoteObject>& remoteObject)
{
    lock_guard<recursive_mutex> autoLock(listenerMapLock_);
    for (auto& item : listenerMap_) {
        auto& listeners = item.second;
        UnSubscribeSystemAbilityLocked(listeners, remoteObject);
    }
    HILOGI("UnSubscribeSystemAbility remote object dead!");
}

void SystemAbilityManager::SetDeviceName(const u16string &name)
{
    deviceName_ = name;
}

const u16string& SystemAbilityManager::GetDeviceName() const
{
    return deviceName_;
}

void SystemAbilityManager::NotifyRemoteSaDied(const std::u16string& name)
{
    std::u16string saName;
    std::string deviceId;
    ParseRemoteSaName(name, deviceId, saName);
    if (dBinderService_ != nullptr) {
        std::string nodeId = TransformDeviceId(deviceId, NODE_ID, false);
        dBinderService_->NoticeServiceDie(saName, nodeId);
        HILOGD("NotifyRemoteSaDied, serviceName is %s, deviceId is %s",
            Str16ToStr8(saName).c_str(), nodeId.c_str());
    }
}

void SystemAbilityManager::NotifyRemoteDeviceOffline(const std::string& deviceId)
{
    if (dBinderService_ != nullptr) {
        dBinderService_->NoticeDeviceDie(deviceId);
        HILOGD("NotifyRemoteDeviceOffline, deviceId is %s", deviceId.c_str());
    }
}

void SystemAbilityManager::ParseRemoteSaName(const std::u16string& name, std::string& deviceId, std::u16string& saName)
{
    vector<string> strVector;
    SplitStr(Str16ToStr8(name), "_", strVector);
    if (strVector.size() == SPLIT_NAME_VECTOR_SIZE) {
        deviceId = strVector[0];
        saName = Str8ToStr16(strVector[1]);
    }
}

int32_t SystemAbilityManager::AddSystemAbility(int32_t systemAbilityId, const sptr<IRemoteObject>& ability,
    const SAExtraProp& extraProp)
{
    HILOGI("%s called", __func__);
    if (!CheckInputSysAbilityId(systemAbilityId) || ability == nullptr) {
        HILOGE("AddSystemAbilityExtra input params is invalid.");
        return ERR_INVALID_VALUE;
    }
    {
        unique_lock<shared_mutex> writeLock(abilityMapLock_);
        auto saSize = abilityMap_.size();
        if (saSize >= MAX_SERVICES) {
            HILOGE("map size error, (Has been greater than %zu)", saSize);
            return ERR_INVALID_VALUE;
        }
        SAInfo saInfo;
        saInfo.remoteObj = ability;
        saInfo.isDistributed = extraProp.isDistributed;
        saInfo.capability = extraProp.capability;
        saInfo.permission = Str16ToStr8(extraProp.permission);
        abilityMap_[systemAbilityId] = std::move(saInfo);
        HILOGI("insert %{public}d. size : %{public}zu", systemAbilityId, abilityMap_.size());
    }
    RemoveCheckLoadedMsg(systemAbilityId);
    if (abilityDeath_ != nullptr) {
        ability->AddDeathRecipient(abilityDeath_);
    }

    u16string strName = Str8ToStr16(to_string(systemAbilityId));
    if (extraProp.isDistributed && dBinderService_ != nullptr) {
        dBinderService_->RegisterRemoteProxy(strName, systemAbilityId);
        HILOGD("AddSystemAbility RegisterRemoteProxy, serviceId is %{public}d", systemAbilityId);
    }
    if (systemAbilityId == SOFTBUS_SERVER_SA_ID && !isDbinderStart_) {
        if (dBinderService_ != nullptr && rpcCallbackImp_ != nullptr) {
            bool ret = dBinderService_->StartDBinderService(rpcCallbackImp_);
            HILOGI("start result is %{public}s", ret ? "succeed" : "fail");
            isDbinderStart_ = true;
        }
    }
    SendSystemAbilityAddedMsg(systemAbilityId, ability);
    return ERR_OK;
}

int32_t SystemAbilityManager::AddSystemProcess(const u16string& procName,
    const sptr<IRemoteObject>& procObject)
{
    if (procName.empty() || procObject == nullptr) {
        HILOGE("AddSystemProcess empty name or null object!");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    size_t procNum = systemProcessMap_.size();
    if (procNum >= MAX_SERVICES) {
        HILOGE("AddSystemProcess map size reach MAX_SERVICES already");
        return ERR_INVALID_VALUE;
    }
    systemProcessMap_[procName] = procObject;
    if (systemProcessDeath_ != nullptr) {
        bool ret = procObject->AddDeathRecipient(systemProcessDeath_);
        HILOGW("AddSystemProcess AddDeathRecipient %{public}s!", ret ? "succeed" : "failed");
    }
    HILOGI("AddSystemProcess insert %{public}s. size : %{public}zu", Str16ToStr8(procName).c_str(),
        systemProcessMap_.size());
    auto iterStarting = startingProcessMap_.find(procName);
    if (iterStarting != startingProcessMap_.end()) {
        int64_t end = GetTickCount();
        HILOGI("[PerformanceTest] AddSystemProcess start process:%{public}s spend %{public}" PRId64 " ms",
            Str16ToStr8(procName).c_str(), (end - iterStarting->second));
        startingProcessMap_.erase(iterStarting);
    }
    return ERR_OK;
}

int32_t SystemAbilityManager::RemoveSystemProcess(const sptr<IRemoteObject>& procObject)
{
    HILOGI("RemoveSystemProcess called");
    if (procObject == nullptr) {
        HILOGW("RemoveSystemProcess null object!");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    for (const auto& [procName, object] : systemProcessMap_) {
        if (object == procObject) {
            if (systemProcessDeath_ != nullptr) {
                procObject->RemoveDeathRecipient(systemProcessDeath_);
            }
            std::string name = Str16ToStr8(procName);
            (void)systemProcessMap_.erase(procName);
            HILOGI("RemoveSystemProcess process:%{public}s dead, size : %{public}zu", name.c_str(),
                systemProcessMap_.size());
            break;
        }
    }
    return ERR_OK;
}

sptr<IRemoteObject> SystemAbilityManager::GetSystemProcess(const u16string& procName)
{
    if (procName.empty()) {
        HILOGE("GetSystemProcess empty name!");
        return nullptr;
    }

    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    auto iter = systemProcessMap_.find(procName);
    if (iter != systemProcessMap_.end()) {
        HILOGI("process:%{public}s found", Str16ToStr8(procName).c_str());
        return iter->second;
    }
    HILOGE("process:%{public}s not exist", Str16ToStr8(procName).c_str());
    return nullptr;
}

void SystemAbilityManager::SendSystemAbilityAddedMsg(int32_t systemAbilityId, const sptr<IRemoteObject>& remoteObject)
{
    if (workHandler_ == nullptr) {
        HILOGE("SendSystemAbilityAddedMsg work handler not initialized!");
        return;
    }
    auto notifyAddedTask = [systemAbilityId, remoteObject, this]() {
        FindSystemAbilityNotify(systemAbilityId, ADD_SYSTEM_ABILITY_TRANSACTION);
        NotifySystemAbilityLoaded(systemAbilityId, remoteObject);
    };
    bool ret = workHandler_->PostTask(notifyAddedTask);
    if (!ret) {
        HILOGW("SendSystemAbilityAddedMsg PostTask failed!");
    }
}

void SystemAbilityManager::SendSystemAbilityRemovedMsg(int32_t systemAbilityId)
{
    if (workHandler_ == nullptr) {
        HILOGE("SendSystemAbilityRemovedMsg work handler not initialized!");
        return;
    }
    auto notifyRemovedTask = [systemAbilityId, this]() {
        FindSystemAbilityNotify(systemAbilityId, REMOVE_SYSTEM_ABILITY_TRANSACTION);
    };
    bool ret = workHandler_->PostTask(notifyRemovedTask);
    if (!ret) {
        HILOGW("SendSystemAbilityRemovedMsg PostTask failed!");
    }
}

void SystemAbilityManager::SendCheckLoadedMsg(int32_t systemAbilityId, const std::u16string& name,
    const sptr<ISystemAbilityLoadCallback>& callback)
{
    if (workHandler_ == nullptr) {
        HILOGE("SendCheckLoadedMsg work handler not initialized!");
        return;
    }

    auto delayTask = [systemAbilityId, name, callback, this]() {
        HILOGI("SendCheckLoadedMsg handle for SA : %{public}d.", systemAbilityId);
        if (CheckSystemAbility(systemAbilityId) != nullptr) {
            HILOGI("SendCheckLoadedMsg SA : %{public}d loaded.", systemAbilityId);
            return;
        }
        {
            lock_guard<recursive_mutex> autoLock(onDemandLock_);
            auto iter = startingAbilityMap_.find(systemAbilityId);
            if (iter == startingAbilityMap_.end()) {
                HILOGI("SendCheckLoadedMsg SA : %{public}d not in startingAbilityMap.", systemAbilityId);
                return;
            }
            auto& abilityItem = iter->second;
            for (auto& callbackItem : abilityItem.callbackList) {
                if (callback->AsObject() == callbackItem.first->AsObject()) {
                    NotifySystemAbilityLoadFail(systemAbilityId, callbackItem.first);
                    RemoveStartingAbilityCallbackLocked(callbackItem);
                    abilityItem.callbackList.remove(callbackItem);
                    break;
                }
            }
            if (abilityItem.callbackList.empty()) {
                HILOGD("SendCheckLoadedMsg startingAbilityMap remove SA : %{public}d.", systemAbilityId);
                startingAbilityMap_.erase(iter);
            }
            auto iterStarting = startingProcessMap_.find(name);
            if (iterStarting != startingProcessMap_.end()) {
                HILOGI("SendCheckLoadedMsg clean process:%{public}s", Str16ToStr8(name).c_str());
                startingProcessMap_.erase(iterStarting);
            }
        }
        (void)GetSystemProcess(name);
    };
    HILOGI("SendCheckLoadedMsg PostTask name : %{public}d!", systemAbilityId);
    bool ret = workHandler_->PostTask(delayTask, ToString(systemAbilityId), CHECK_LOADED_DELAY_TIME);
    if (!ret) {
        HILOGW("SendCheckLoadedMsg PostTask failed!");
    }
}

void SystemAbilityManager::RemoveCheckLoadedMsg(int32_t systemAbilityId)
{
    if (workHandler_ == nullptr) {
        HILOGE("RemoveCheckLoadedMsg work handler not initialized!");
        return;
    }
    HILOGI("RemoveCheckLoadedMsg sa : %{public}d!", systemAbilityId);
    workHandler_->RemoveTask(ToString(systemAbilityId));
}

void SystemAbilityManager::NotifySystemAbilityLoaded(int32_t systemAbilityId, const sptr<IRemoteObject>& remoteObject,
    const sptr<ISystemAbilityLoadCallback>& callback)
{
    if (callback == nullptr) {
        HILOGE("NotifySystemAbilityLoaded callback null!");
        return;
    }
    callback->OnLoadSystemAbilitySuccess(systemAbilityId, remoteObject);
}

void SystemAbilityManager::NotifySystemAbilityLoaded(int32_t systemAbilityId, const sptr<IRemoteObject>& remoteObject)
{
    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    auto iter = startingAbilityMap_.find(systemAbilityId);
    if (iter == startingAbilityMap_.end()) {
        return;
    }
    auto& abilityItem = iter->second;
    for (auto& callbackItem : abilityItem.callbackList) {
        NotifySystemAbilityLoaded(systemAbilityId, remoteObject, callbackItem.first);
        RemoveStartingAbilityCallbackLocked(callbackItem);
    }
    startingAbilityMap_.erase(iter);
}

void SystemAbilityManager::NotifySystemAbilityLoadFail(int32_t systemAbilityId,
    const sptr<ISystemAbilityLoadCallback>& callback)
{
    if (callback == nullptr) {
        HILOGE("NotifySystemAbilityLoadFail callback null!");
        return;
    }
    callback->OnLoadSystemAbilityFail(systemAbilityId);
}

int32_t SystemAbilityManager::StartDynamicSystemProcess(const std::u16string& name, int32_t systemAbilityId)
{
    std::string strExtra = std::to_string(systemAbilityId);
    auto extraArgv = strExtra.c_str();
    auto result = ServiceControlWithExtra(Str16ToStr8(name).c_str(), START, &extraArgv, 1);
    HILOGI("StartDynamicSystemProcess call ServiceControlWithExtra result:%{public}d!", result);
    if (result == 0) {
        return ERR_OK;
    }
    return ERR_INVALID_VALUE;
}

int32_t SystemAbilityManager::StartingSystemProcess(const std::u16string& procName, int32_t systemAbilityId)
{
    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    if (startingProcessMap_.count(procName) != 0) {
        HILOGI("StartingSystemProcess process:%{public}s already starting!", Str16ToStr8(procName).c_str());
        return ERR_OK;
    }
    auto iter = systemProcessMap_.find(procName);
    if (iter != systemProcessMap_.end()) {
        StartOnDemandAbility(systemAbilityId);
        return ERR_OK;
    }
    // call init start process
    int64_t begin = GetTickCount();
    int32_t result = StartDynamicSystemProcess(procName, systemAbilityId);
    if (result == ERR_OK) {
        startingProcessMap_.emplace(procName, begin);
    }
    return result;
}

int32_t SystemAbilityManager::LoadSystemAbility(int32_t systemAbilityId,
    const sptr<ISystemAbilityLoadCallback>& callback)
{
    if (!CheckInputSysAbilityId(systemAbilityId) || callback == nullptr) {
        HILOGW("LoadSystemAbility systemAbilityId or callback invalid!");
        return ERR_INVALID_VALUE;
    }
    SaProfile saProfile;
    bool ret = GetSaProfile(systemAbilityId, saProfile);
    if (!ret) {
        HILOGE("LoadSystemAbility systemAbilityId:%{public}d not supported!", systemAbilityId);
        return ERR_INVALID_VALUE;
    }

    sptr<IRemoteObject> targetObject = CheckSystemAbility(systemAbilityId);
    if (targetObject != nullptr) {
        NotifySystemAbilityLoaded(systemAbilityId, targetObject, callback);
        return ERR_OK;
    }
    int32_t result = ERR_INVALID_VALUE;
    auto callingPid = IPCSkeleton::GetCallingPid();
    {
        lock_guard<recursive_mutex> autoLock(onDemandLock_);
        auto& abilityItem = startingAbilityMap_[systemAbilityId];
        for (const auto& itemCallback : abilityItem.callbackList) {
            if (callback->AsObject() == itemCallback.first->AsObject()) {
                HILOGI("LoadSystemAbility already existed callback object systemAbilityId:%{public}d", systemAbilityId);
                return ERR_OK;
            }
        }
        auto& count = callbackCountMap_[callingPid];
        if (count >= MAX_SUBSCRIBE_COUNT) {
            HILOGE("LoadSystemAbility pid:%{public}d overflow max callback count!", callingPid);
            return ERR_PERMISSION_DENIED;
        }
        ++count;
        abilityItem.callbackList.emplace_back(callback, callingPid);
        if (abilityCallbackDeath_ != nullptr) {
            ret = callback->AsObject()->AddDeathRecipient(abilityCallbackDeath_);
            HILOGI("LoadSystemAbility systemAbilityId:%{public}d AddDeathRecipient %{public}s",
                systemAbilityId, ret ? "succeed" : "failed");
        }
        result = StartingSystemProcess(saProfile.process, systemAbilityId);
        HILOGI("LoadSystemAbility systemAbilityId:%{public}d size : %{public}zu",
            systemAbilityId, abilityItem.callbackList.size());
    }
    SendCheckLoadedMsg(systemAbilityId, saProfile.process, callback);
    return result;
}

void SystemAbilityManager::RemoveStartingAbilityCallbackLocked(
    std::pair<sptr<ISystemAbilityLoadCallback>, int32_t>& itemPair)
{
    if (abilityCallbackDeath_ != nullptr) {
        itemPair.first->AsObject()->RemoveDeathRecipient(abilityCallbackDeath_);
    }
    auto iterCount = callbackCountMap_.find(itemPair.second);
    if (iterCount != callbackCountMap_.end()) {
        --iterCount->second;
        if (iterCount->second == 0) {
            callbackCountMap_.erase(iterCount);
        }
    }
}

void SystemAbilityManager::RemoveStartingAbilityCallback(AbilityItem& abilityItem,
    const sptr<IRemoteObject>& remoteObject)
{
    auto& callbacks = abilityItem.callbackList;
    auto iterCallback = callbacks.begin();
    while (iterCallback != callbacks.end()) {
        auto& callbackPair = *iterCallback;
        if (callbackPair.first->AsObject() == remoteObject) {
            RemoveStartingAbilityCallbackLocked(callbackPair);
            iterCallback = callbacks.erase(iterCallback);
            break;
        } else {
            ++iterCallback;
        }
    }
}

void SystemAbilityManager::OnAbilityCallbackDied(const sptr<IRemoteObject>& remoteObject)
{
    HILOGI("OnAbilityCallbackDied received remoteObject died message!");
    lock_guard<recursive_mutex> autoLock(onDemandLock_);
    auto iter = startingAbilityMap_.begin();
    while (iter != startingAbilityMap_.end()) {
        AbilityItem& abilityItem = iter->second;
        RemoveStartingAbilityCallback(abilityItem, remoteObject);
        if (abilityItem.callbackList.empty()) {
            iter = startingAbilityMap_.erase(iter);
        } else {
            ++iter;
        }
    }
}

std::string SystemAbilityManager::TransformDeviceId(const std::string& deviceId, int32_t type, bool isPrivate)
{
    return isPrivate ? std::string() : deviceId;
}

std::string SystemAbilityManager::GetLocalNodeId()
{
    return std::string();
}
} // namespace OHOS
