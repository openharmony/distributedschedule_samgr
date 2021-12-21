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

#ifndef SERVICES_SAMGR_NATIVE_INCLUDE_SYSTEM_ABILITY_MANAGER_H_
#define SERVICES_SAMGR_NATIVE_INCLUDE_SYSTEM_ABILITY_MANAGER_H_

#include "system_ability_manager_stub.h"
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <utility>

#include "event_handler.h"
#include "dbinder_service.h"
#include "dbinder_service_stub.h"
#include "sa_profiles.h"
#include "system_ability_definition.h"

namespace OHOS {
struct SAInfo {
    sptr<IRemoteObject> remoteObj;
    bool isDistributed = false;
    std::u16string capability;
    std::string permission;
};

enum {
    UUID = 0,
    NODE_ID,
    UNKNOWN,
};

class SystemAbilityManager : public SystemAbilityManagerStub {
public:
    virtual ~SystemAbilityManager();
    static sptr<SystemAbilityManager> GetInstance();

    int32_t RemoveSystemAbility(const sptr<IRemoteObject>& ability);
    std::vector<std::u16string> ListSystemAbilities(uint32_t dumpFlags) override;

    void SetDeviceName(const std::u16string &name);

    const std::u16string& GetDeviceName() const;

    const sptr<DBinderService> GetDBinder() const;

    sptr<IRemoteObject> GetSystemAbility(int32_t systemAbilityId) override;

    sptr<IRemoteObject> CheckSystemAbility(int32_t systemAbilityId) override;

    int32_t RemoveSystemAbility(int32_t systemAbilityId) override;

    int32_t SubscribeSystemAbility(int32_t systemAbilityId, const sptr<ISystemAbilityStatusChange>& listener) override;
    int32_t UnSubscribeSystemAbility(int32_t systemAbilityId,
        const sptr<ISystemAbilityStatusChange>& listener) override;
    void UnSubscribeSystemAbility(const sptr<IRemoteObject>& remoteObject);

    sptr<IRemoteObject> GetSystemAbility(int32_t systemAbilityId, const std::string& deviceId) override;

    sptr<IRemoteObject> CheckSystemAbility(int32_t systemAbilityId, const std::string& deviceId) override;

    int32_t AddOnDemandSystemAbilityInfo(int32_t systemAbilityId,
        const std::u16string& localAbilityManagerName) override;

    sptr<IRemoteObject> CheckSystemAbility(int32_t systemAbilityId, bool& isExist) override;

    void NotifyRemoteSaDied(const std::u16string& name);
    void NotifyRemoteDeviceOffline(const std::string& deviceId);
    int32_t AddSystemAbility(int32_t systemAbilityId, const sptr<IRemoteObject>& ability,
        const SAExtraProp& extraProp) override;
    std::string TransformDeviceId(const std::string& deviceId, int32_t type, bool isPrivate);
    std::string GetLocalNodeId();
    void Init();

    int32_t AddSystemProcess(const std::u16string& procName, const sptr<IRemoteObject>& procObject) override;
    int32_t RemoveSystemProcess(const sptr<IRemoteObject>& procObject);
private:
    SystemAbilityManager();
    std::u16string GetSystemAbilityName(int32_t index) override;
    void DoInsertSaData(const std::u16string& name, const sptr<IRemoteObject>& ability, const SAExtraProp& extraProp);
    bool IsNameInValid(const std::u16string& name);
    int32_t StartOnDemandAbility(int32_t systemAbilityId);
    void DeleteStartingAbilityMember(int32_t systemAbilityId);
    void ParseRemoteSaName(const std::u16string& name, std::string& deviceId, std::u16string& saName);
    bool IsLocalDeviceId(const std::string& deviceId);
    bool CheckDistributedPermission();
    int32_t AddSystemAbility(const std::u16string& name, const sptr<IRemoteObject>& ability,
        const SAExtraProp& extraProp);
    int32_t FindSystemAbilityNotify(int32_t systemAbilityId, int32_t code);
    int32_t FindSystemAbilityNotify(int32_t systemAbilityId, const std::string& deviceId, int32_t code);

    sptr<IRemoteObject> GetSystemProcess(const std::u16string& procName);
    sptr<IRemoteObject> CheckLocalAbilityManager(const std::u16string& name);
    void InitSaProfile();
    bool GetSaProfile(int32_t saId, SaProfile& saProfile);
    void NotifySystemAbilityChanged(int32_t systemAbilityId, const std::string& deviceId, int32_t code,
        const sptr<ISystemAbilityStatusChange>& listener);
    void UnSubscribeSystemAbilityLocked(std::list<std::pair<sptr<ISystemAbilityStatusChange>, int32_t>>& listenerList,
        const sptr<IRemoteObject>& listener);

    std::u16string deviceName_;
    static sptr<SystemAbilityManager> instance;
    static std::mutex instanceLock;
    sptr<IRemoteObject::DeathRecipient> abilityDeath_;
    sptr<IRemoteObject::DeathRecipient> systemProcessDeath_;
    sptr<IRemoteObject::DeathRecipient> abilityStatusDeath_;
    sptr<DBinderService> dBinderService_;
    bool isDbinderStart_ = false;

    // must hold abilityMapLock_ never access other locks
    std::shared_mutex abilityMapLock_;
    std::map<int32_t, SAInfo> abilityMap_;

    // must hold systemProcessMapLock_ never access other locks
    std::recursive_mutex systemProcessMapLock_;
    std::map<std::u16string, sptr<IRemoteObject>> systemProcessMap_;

    // maybe hold listenerMapLock_ and then access systemProcessMapLock_
    std::recursive_mutex listenerMapLock_;
    std::map<int32_t, std::list<std::pair<sptr<ISystemAbilityStatusChange>, int32_t>>> listenerMap_;
    std::map<int32_t, int32_t> subscribeCountMap_;

    // maybe hold onDemandAbilityMapLock_ and then access systemProcessMapLock_
    std::recursive_mutex onDemandAbilityMapLock_;
    std::map<int32_t, std::u16string> onDemandAbilityMap_;
    std::list<int32_t> startingAbilityList_;

    std::shared_ptr<AppExecFwk::EventHandler> parseHandler_;

    std::map<int32_t, SaProfile> saProfileMap_;
    std::mutex saProfileMapLock_;
};
} // namespace OHOS

#endif // !defined(SERVICES_SAMGR_NATIVE_INCLUDE_SYSTEM_ABILITY_MANAGER_H_)
