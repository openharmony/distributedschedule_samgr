/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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
#include "local_ability_manager_proxy.h"
#include "parse_util.h"

#include "sam_log.h"
#include "string_ex.h"
#include "ipc_skeleton.h"
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
    if (parseHandler_ == nullptr) {
        auto parseRunner = AppExecFwk::EventRunner::Create("ParseHandler");
        parseHandler_ = make_shared<AppExecFwk::EventHandler>(parseRunner);
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
    if (parseHandler_ == nullptr) {
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
    bool ret = parseHandler_->PostTask(callback);
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
    HILOGI("NOT found service : %{public}d", systemAbilityId);
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

int32_t SystemAbilityManager::AddOnDemandSystemAbilityInfo(int32_t systemAbilityId,
    const std::u16string& localAbilityManagerName)
{
    HILOGI("%{public}s called", __func__);
    if (!CheckInputSysAbilityId(systemAbilityId) || IsNameInValid(localAbilityManagerName)) {
        HILOGI("var is invalid.");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(onDemandAbilityMapLock_);
    auto onDemandSaSize = onDemandAbilityMap_.size();
    if (onDemandSaSize >= MAX_SERVICES) {
        HILOGE("map size error, (Has been greater than %{public}zu)",
            onDemandAbilityMap_.size());
        return ERR_INVALID_VALUE;
    }

    onDemandAbilityMap_[systemAbilityId] = localAbilityManagerName;
    HILOGI("insert %{public}d. size : %{public}zu", systemAbilityId, onDemandAbilityMap_.size());
    return ERR_OK;
}

int32_t SystemAbilityManager::StartOnDemandAbility(int32_t systemAbilityId)
{
    HILOGI("%{public}s called, systemAbilityId is %{public}d", __func__, systemAbilityId);
    lock_guard<recursive_mutex> onDemandAbilityLock(onDemandAbilityMapLock_);
    auto iter = onDemandAbilityMap_.find(systemAbilityId);
    if (iter != onDemandAbilityMap_.end()) {
        HILOGI("found onDemandAbility: %{public}d.", systemAbilityId);
        sptr<ILocalAbilityManager> procObject =
            iface_cast<ILocalAbilityManager>(GetSystemProcess(iter->second));
        if (procObject == nullptr) {
            HILOGI("get process:%{public}s fail", Str16ToStr8(iter->second).c_str());
            return ERR_OK;
        }
        procObject->StartAbility(systemAbilityId);
        startingAbilityList_.emplace_back(systemAbilityId);
        return ERR_OK;
    }

    return ERR_INVALID_VALUE;
}

sptr<IRemoteObject> SystemAbilityManager::CheckSystemAbility(int32_t systemAbilityId, bool& isExist)
{
    if (!CheckInputSysAbilityId(systemAbilityId)) {
        return nullptr;
    }
    sptr<IRemoteObject> abilityProxy = CheckSystemAbility(systemAbilityId);
    if (abilityProxy == nullptr) {
        {
            lock_guard<recursive_mutex> autoLock(onDemandAbilityMapLock_);
            for (int32_t startingAbilityId : startingAbilityList_) {
                if (startingAbilityId == systemAbilityId) {
                    isExist = true;
                    return nullptr;
                }
            }
        }

        int32_t ret = StartOnDemandAbility(systemAbilityId);
        if (ret == ERR_OK) {
            isExist = true;
            return nullptr;
        }

        HILOGI("ability %{public}d is not found", systemAbilityId);
        isExist = false;
        return nullptr;
    }

    isExist = true;
    return abilityProxy;
}

void SystemAbilityManager::DeleteStartingAbilityMember(int32_t systemAbilityId)
{
    if (!CheckInputSysAbilityId(systemAbilityId)) {
        return;
    }
    lock_guard<recursive_mutex> autoLock(onDemandAbilityMapLock_);
    for (auto iter = startingAbilityList_.begin(); iter != startingAbilityList_.end();) {
        if (*iter == systemAbilityId) {
            iter = startingAbilityList_.erase(iter);
        } else {
            ++iter;
        }
    }
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

    FindSystemAbilityNotify(systemAbilityId, REMOVE_SYSTEM_ABILITY_TRANSACTION);
    DeleteStartingAbilityMember(systemAbilityId);
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
        FindSystemAbilityNotify(saId, REMOVE_SYSTEM_ABILITY_TRANSACTION);
        DeleteStartingAbilityMember(saId);
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

u16string SystemAbilityManager::GetSystemAbilityName(int32_t index)
{
    shared_lock<shared_mutex> readLock(abilityMapLock_);
    if (index < 0 || static_cast<uint32_t>(index) >= abilityMap_.size()) {
        return u16string();
    }
    int32_t count = 0;
    for (auto iter = abilityMap_.begin(); iter != abilityMap_.end(); iter++) {
        if (count++ == index) {
            return Str8ToStr16(to_string(iter->first));
        }
    }
    return u16string();
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
    if (abilityDeath_ != nullptr) {
        ability->AddDeathRecipient(abilityDeath_);
    }
    FindSystemAbilityNotify(systemAbilityId, ADD_SYSTEM_ABILITY_TRANSACTION);
    u16string strName = Str8ToStr16(to_string(systemAbilityId));
    if (extraProp.isDistributed && dBinderService_ != nullptr) {
        dBinderService_->RegisterRemoteProxy(strName, systemAbilityId);
        HILOGD("AddSystemAbility RegisterRemoteProxy, serviceId is %{public}d", systemAbilityId);
    }
    if (systemAbilityId == SOFTBUS_SERVER_SA_ID && !isDbinderStart_) {
        if (dBinderService_ != nullptr) {
            bool ret = dBinderService_->StartDBinderService();
            HILOGI("start result is %{public}s", ret ? "succeed" : "fail");
            isDbinderStart_ = true;
        }
    }
    return ERR_OK;
}

int32_t SystemAbilityManager::AddSystemProcess(const u16string& procName,
    const sptr<IRemoteObject>& procObject)
{
    if (procName.empty() || procObject == nullptr) {
        HILOGE("AddSystemProcess empty name or null object!");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(systemProcessMapLock_);
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
    return ERR_OK;
}

int32_t SystemAbilityManager::RemoveSystemProcess(const sptr<IRemoteObject>& procObject)
{
    HILOGI("RemoveSystemProcess called");
    if (procObject == nullptr) {
        HILOGW("RemoveSystemProcess null object!");
        return ERR_INVALID_VALUE;
    }

    lock_guard<recursive_mutex> autoLock(systemProcessMapLock_);
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

    lock_guard<recursive_mutex> autoLock(systemProcessMapLock_);
    auto iter = systemProcessMap_.find(procName);
    if (iter != systemProcessMap_.end()) {
        HILOGI("process:%{public}s found", Str16ToStr8(procName).c_str());
        return iter->second;
    }
    HILOGE("process:%{public}s not exist", Str16ToStr8(procName).c_str());
    return nullptr;
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
