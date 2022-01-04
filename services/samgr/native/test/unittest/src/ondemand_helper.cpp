/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "ondemand_helper.h"

#include <iostream>
#include "datetime_ex.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"

using namespace OHOS;
using namespace std;

namespace OHOS {
OnDemandHelper::OnDemandHelper()
{
    loadCallback_ = new OnDemandLoadCallback();
}

OnDemandHelper& OnDemandHelper::GetInstance()
{
    static OnDemandHelper instance;
    return instance;
}

int32_t OnDemandHelper::OnDemandAbility(int32_t systemAbilityId)
{
    sptr<ISystemAbilityManager> sm = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sm == nullptr) {
        cout << "GetSystemAbilityManager samgr object null!" << endl;
        return ERR_NULL_OBJECT;
    }
    int32_t result = sm->LoadSystemAbility(systemAbilityId, loadCallback_);
    if (result != ERR_OK) {
        cout << "systemAbilityId:" << systemAbilityId << " load failed, result code:" << result << endl;
        return result;
    }
    return ERR_OK;
}

sptr<IRemoteObject> OnDemandHelper::GetSystemAbility(int32_t systemAbilityId)
{
    sptr<ISystemAbilityManager> sm = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sm == nullptr) {
        cout << "GetSystemAbilityManager samgr object null!" << endl;
        return nullptr;
    }
    sptr<IRemoteObject> remoteObject = sm->GetSystemAbility(systemAbilityId);
    if (remoteObject == nullptr) {
        cout << "GetSystemAbility systemAbilityId:" << systemAbilityId << " failed !" << endl;
        return nullptr;
    }
    return remoteObject;
}

void OnDemandHelper::OnLoadSystemAbility(int32_t systemAbilityId)
{
}

void OnDemandHelper::OnDemandLoadCallback::OnLoadSystemAbilitySuccess(int32_t systemAbilityId,
    const sptr<IRemoteObject>& remoteObject)
{
    cout << "OnLoadSystemAbilitySuccess systemAbilityId:" << systemAbilityId << " IRemoteObject result:" <<
        ((remoteObject != nullptr) ? "succeed" : "failed") << endl;
    OnDemandHelper::GetInstance().OnLoadSystemAbility(systemAbilityId);
}
}

int main(int argc, char* argv[])
{
    OHOS::OnDemandHelper& ondemandHelper = OnDemandHelper::GetInstance();
    string cmd = "load";
    do {
        if (cmd != "load" && cmd != "get") {
            cmd = "load";
        }
        int32_t systemAbilityId = 0;
        cout << "please input systemAbilityId for " << cmd << " operation" << endl;
        cin >> systemAbilityId;
        if (systemAbilityId < FIRST_SYS_ABILITY_ID || systemAbilityId > LAST_SYS_ABILITY_ID) {
            cout << "input systemAbilityId:" << systemAbilityId << " invalid!" << endl;
            break;
        }
        int64_t begin = GetTickCount();
        if (cmd == "get") {
            auto remoteObject = ondemandHelper.GetSystemAbility(systemAbilityId);
            cout << "GetSystemAbility result:" << ((remoteObject != nullptr) ? "succeed" : "failed") << " spend:"
                << (GetTickCount() - begin) << " ms" << " systemAbilityId:" << systemAbilityId << endl;
        } else {
            int32_t ret = ondemandHelper.OnDemandAbility(systemAbilityId);
            cout << "LoadSystemAbility result:" << ret << " spend:" << (GetTickCount() - begin) << " ms"
                << " systemAbilityId:" << systemAbilityId << endl;
        }

        cout << "-----Input q or Q to quit, [load] for LoadSystemAbility, [get] for GetSystemAbility-----" << endl;
        cmd.clear();
        cin >> cmd;
        cin.clear();
        cin.ignore(numeric_limits<std::streamsize>::max(), '\n');
    } while (cmd[0] != 'q' && cmd[0] != 'Q');
    cout << "see you, bye!" << endl;
    return 0;
}
