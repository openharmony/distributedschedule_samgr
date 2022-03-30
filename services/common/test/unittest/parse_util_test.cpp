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

#include "gtest/gtest.h"
#include "parse_util.h"
#include "string_ex.h"
#include "test_log.h"

using namespace std;
using namespace testing;
using namespace testing::ext;

namespace OHOS {
namespace SAMGR {
namespace {
    const std::string TEST_RESOURCE_PATH = "/data/test/resource/samgr/profile/";
    const std::u16string TEST_PROCESS_NAME = u"sa_test";
    const int32_t TEST_PROFILE_SAID = 9999;
}

class ParseUtilTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
protected:
    std::shared_ptr<ParseUtil> parser_;
};

void ParseUtilTest::SetUpTestCase()
{
    DTEST_LOG << "SetUpTestCase" << std::endl;
}

void ParseUtilTest::TearDownTestCase()
{
    DTEST_LOG << "TearDownTestCase" << std::endl;
}

void ParseUtilTest::SetUp()
{
    DTEST_LOG << "SetUp" << std::endl;
    if (parser_ == nullptr) {
        parser_ = std::make_shared<ParseUtil>();
    }
}

void ParseUtilTest::TearDown()
{
    DTEST_LOG << "TearDown" << std::endl;
    if (parser_ != nullptr) {
        parser_->ClearResource();
    }
}

/**
 * @tc.name: ParseSaProfile001
 * @tc.desc: Verify if can load not exist file
 * @tc.type: FUNC
 * @tc.require: AR000GGVQF
 */
HWTEST_F(ParseUtilTest, ParseSaProfile001, TestSize.Level1)
{
    DTEST_LOG << " ParseSaProfile001 start " << std::endl;
    /**
     * @tc.steps: step1. parse not exsit config file
     * @tc.expected: step1. return false when load not exist file
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "notExist");
    EXPECT_FALSE(ret);
}

/**
 * @tc.name: ParseSaProfile002
 * @tc.desc: Verify if can load invalid root file
 * @tc.type: FUNC
 * @tc.require: SR000GGVP1
 */
HWTEST_F(ParseUtilTest, ParseSaProfile002, TestSize.Level1)
{
    DTEST_LOG << " ParseSaProfile002 start " << std::endl;
    /**
     * @tc.steps: step1. load invalid root config file
     * @tc.expected: step1. return false when load invalid format config file
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "invalid_root.xml");
    EXPECT_FALSE(ret);
}

/**
 * @tc.name: ParseSaProfile003
 * @tc.desc: Verify if can load normal profile
 * @tc.type: FUNC
 * @tc.require: AR000GGVQF
 */
HWTEST_F(ParseUtilTest, ParseSaProfile003, TestSize.Level1)
{
    DTEST_LOG << " ParseSaProfile003 start " << std::endl;
    /**
     * @tc.steps: step1. load correct system ability profile
     * @tc.expected: step1. return true when load invalid format config file
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "profile.xml");
    EXPECT_TRUE(ret);
}

/**
 * @tc.name: GetSaProfiles001
 * @tc.desc:  Verify if not load sa file return empty list
 * @tc.type: FUNC
 * @tc.require: SR000GGVP1
 */
HWTEST_F(ParseUtilTest, GetSaProfiles001, TestSize.Level1)
{
    DTEST_LOG << " GetSaProfiles001 start " << std::endl;
    /**
     * @tc.steps: step1. Get empty config when not parse sa file.
     * @tc.expected: step1. return empty list when not load sa file
     */
    list<SaProfile> profiles = parser_->GetAllSaProfiles();
    EXPECT_TRUE(profiles.empty());
}

/**
 * @tc.name: GetSaProfiles002
 * @tc.desc:  Verify if can load normal sa profile
 * @tc.type: FUNC
 * @tc.require: SR000GGVP1
 */
HWTEST_F(ParseUtilTest, GetSaProfiles002, TestSize.Level1)
{
    DTEST_LOG << " GetSaProfiles002 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct profile when parse sa file.
     * @tc.expected: step1. return correct profile object when load correct config file
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "profile.xml");
    ASSERT_TRUE(ret);
    list<SaProfile> profiles = parser_->GetAllSaProfiles();
    if (!profiles.empty()) {
        SaProfile& profile = *(profiles.begin());
        EXPECT_EQ(profile.process, TEST_PROCESS_NAME);
        EXPECT_EQ(profile.saId, TEST_PROFILE_SAID);
    }
}

/**
 * @tc.name: ParseTrustConfig001
 * @tc.desc:  Verify if can load file with one sa
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, ParseTrustConfig001, TestSize.Level1)
{
    DTEST_LOG << " ParseTrustConfig001 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct map when parse config file.
     * @tc.expected: step1. return correct profile object when load correct config file
     */
    std::map<std::u16string, std::set<int32_t>> values;
    bool ret = parser_->ParseTrustConfig(TEST_RESOURCE_PATH + "test_trust_one_sa.xml", values);
    ASSERT_TRUE(ret);
    /**
     * @tc.steps: step2. Check map values
     * @tc.expected: step2. return expect values
     */
    for (const auto& [process, saIds] : values) {
        EXPECT_EQ(Str16ToStr8(process), "test");
        EXPECT_EQ(saIds.size(), 1);
        EXPECT_EQ(saIds.count(1401), 1);
    }
}

/**
 * @tc.name: ParseTrustConfig002
 * @tc.desc:  Verify if can load file with muti sa
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, ParseTrustConfig002, TestSize.Level1)
{
    DTEST_LOG << " ParseTrustConfig002 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct map when parse config file.
     * @tc.expected: step1. return correct profile object when load correct config file
     */
    std::map<std::u16string, std::set<int32_t>> values;
    bool ret = parser_->ParseTrustConfig(TEST_RESOURCE_PATH + "test_trust_muti_sa.xml", values);
    ASSERT_TRUE(ret);
    /**
     * @tc.steps: step2. Check map values
     * @tc.expected: step2. return expect values
     */
    for (const auto& [process, saIds] : values) {
        EXPECT_EQ(Str16ToStr8(process), "test");
        EXPECT_EQ(saIds.size(), 5);
        EXPECT_EQ(saIds.count(1401), 1);
    }
}

/**
 * @tc.name: ParseTrustConfig003
 * @tc.desc:  Verify if can load not invalid root file
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, ParseTrustConfig003, TestSize.Level1)
{
    DTEST_LOG << " ParseTrustConfig003 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct map when parse config file.
     * @tc.expected: step1. return false when load invalid root file
     */
    std::map<std::u16string, std::set<int32_t>> values;
    bool ret = parser_->ParseTrustConfig(TEST_RESOURCE_PATH + "invalid_root_trust.xml", values);
    ASSERT_FALSE(ret);
}

/**
 * @tc.name: ParseTrustConfig004
 * @tc.desc:  Verify if can load not exist file
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, ParseTrustConfig004, TestSize.Level1)
{
    DTEST_LOG << " ParseTrustConfig004 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct profile when parse sa file.
     * @tc.expected: step1. return false when not exist file
     */
    std::map<std::u16string, std::set<int32_t>> values;
    bool ret = parser_->ParseTrustConfig(TEST_RESOURCE_PATH + "notExist", values);
    ASSERT_FALSE(ret);
}

/**
 * @tc.name: ParseTrustConfig005
 * @tc.desc:  Verify if can load invalid element config
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, ParseTrustConfig005, TestSize.Level1)
{
    DTEST_LOG << " ParseTrustConfig005 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct profile when parse invalid element config.
     * @tc.expected: step1. return correct profile object when load correct config file
     */
    std::map<std::u16string, std::set<int32_t>> values;
    bool ret = parser_->ParseTrustConfig(TEST_RESOURCE_PATH + "invalid_element_trust.xml", values);
    ASSERT_TRUE(ret);
    for (const auto& [process, saIds] : values) {
        EXPECT_EQ(Str16ToStr8(process), "test");
        EXPECT_EQ(saIds.size(), 3);
        EXPECT_EQ(saIds.count(1401), 1);
    }
}

/**
 * @tc.name: ParseTrustConfig006
 * @tc.desc:  Verify if can load invalid muti root file
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, ParseTrustConfig006, TestSize.Level1)
{
    DTEST_LOG << " ParseTrustConfig006 start " << std::endl;
    /**
     * @tc.steps: step1. Get correct profile when parse sa file.
     * @tc.expected: step1. return correct profile object when load correct config file
     */
    std::map<std::u16string, std::set<int32_t>> values;
    bool ret = parser_->ParseTrustConfig(TEST_RESOURCE_PATH + "invalid_muti_root_trust.xml", values);
    ASSERT_FALSE(ret);
}

/**
 * @tc.name: RemoveSaProfile001
 * @tc.desc:  Verify if can remove not-existed id
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, RemoveSaProfile001, TestSize.Level1)
{
    DTEST_LOG << " RemoveSaProfile001 start " << std::endl;
    /**
     * @tc.steps: step1. parse not exsit config file
     * @tc.expected: step1. return false when load not exist file
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "notExist");
    EXPECT_FALSE(ret);
    /**
     * @tc.steps: step2. remove not-existed id
     * @tc.expected: step2. not crash
     */
    parser_->RemoveSaProfile(111);
    auto profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 0);
}

/**
 * @tc.name: RemoveSaProfile002
 * @tc.desc:  Verify if can can remove not-existed id
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, RemoveSaProfile002, TestSize.Level1)
{
    DTEST_LOG << " RemoveSaProfile002 start " << std::endl;
    /**
     * @tc.steps: step1. parse multi-sa profile
     * @tc.expected: step1. return true when load multi-sa profile
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "multi_sa_profile.xml");
    EXPECT_TRUE(ret);
    auto profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 4);
    /**
     * @tc.steps: step2. remove not-existed id
     * @tc.expected: step2. not crash
     */
    parser_->RemoveSaProfile(111);
    profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 4);
}

/**
 * @tc.name: RemoveSaProfile003
 * @tc.desc:  Verify if can remove one existed id
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, RemoveSaProfile003, TestSize.Level1)
{
    DTEST_LOG << " RemoveSaProfile003 start " << std::endl;
    /**
     * @tc.steps: step1. parse multi-sa profile
     * @tc.expected: step1. return true when load multi-sa profile
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "multi_sa_profile.xml");
    EXPECT_TRUE(ret);
    auto profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 4);
    /**
     * @tc.steps: step2. remove one existed id
     * @tc.expected: step2. remove successfully
     */
    parser_->RemoveSaProfile(9999);
    profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 3);
}

/**
 * @tc.name: RemoveSaProfile004
 * @tc.desc:  Verify if can remove one existed id
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, RemoveSaProfile004, TestSize.Level1)
{
    DTEST_LOG << " RemoveSaProfile004 start " << std::endl;
    /**
     * @tc.steps: step1. parse multi-sa profile
     * @tc.expected: step1. return true when load multi-sa profile
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "multi_sa_profile.xml");
    EXPECT_TRUE(ret);
    auto profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 4);
    /**
     * @tc.steps: step2. remove one existed id
     * @tc.expected: step2. remove successfully
     */
    parser_->RemoveSaProfile(9997);
    profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 2);
}

/**
 * @tc.name: RemoveSaProfile005
 * @tc.desc:  Verify if can remove more existed id
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, RemoveSaProfile005, TestSize.Level1)
{
    DTEST_LOG << " RemoveSaProfile004 start " << std::endl;
    /**
     * @tc.steps: step1. parse multi-sa profile
     * @tc.expected: step1. return true when load multi-sa profile
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "multi_sa_profile.xml");
    EXPECT_TRUE(ret);
    auto profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 4);
    /**
     * @tc.steps: step2. remove more existed id
     * @tc.expected: step2. remove successfully
     */
    parser_->RemoveSaProfile(9997);
    parser_->RemoveSaProfile(9998);
    parser_->RemoveSaProfile(9998);
    profiles = parser_->GetAllSaProfiles();
    EXPECT_EQ(profiles.size(), 1);
}

/**
 * @tc.name: CheckPathExist001
 * @tc.desc:  Verify if can check not exist file
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, CheckPathExist001, TestSize.Level1)
{
    DTEST_LOG << " CheckPathExist001 start " << std::endl;
    /**
     * @tc.steps: step1. check not exsit config file
     * @tc.expected: step1. return false when check not exist file
     */
    bool ret = parser_->CheckPathExist(TEST_RESOURCE_PATH + "not_exist.xml");
    EXPECT_FALSE(ret);
}

/**
 * @tc.name: CheckPathExist002
 * @tc.desc:  Verify if can check exist file
 * @tc.type: FUNC
 * @tc.require: AR000GICSU
 */
HWTEST_F(ParseUtilTest, CheckPathExist002, TestSize.Level1)
{
    DTEST_LOG << " CheckPathExist002 start " << std::endl;
    /**
     * @tc.steps: step1. check exsit config file
     * @tc.expected: step1. return true when load not exist file
     */
    bool ret = parser_->CheckPathExist(TEST_RESOURCE_PATH + "multi_sa_profile.xml");
    EXPECT_TRUE(ret);
}

/**
 * @tc.name: GetProfile001
 * @tc.desc:  Verify if can get not-exist profile
 * @tc.type: FUNC
 * @tc.require: AR000GH56I
 */
HWTEST_F(ParseUtilTest, GetProfile001, TestSize.Level1)
{
    DTEST_LOG << " GetProfile001 start " << std::endl;
    /**
     * @tc.steps: step1. check exsit config file
     * @tc.expected: step1. return true when load not exist file
     */
    SaProfile saProfile;
    bool ret = parser_->GetProfile(9999, saProfile);
    EXPECT_FALSE(ret);
}

/**
 * @tc.name: GetProfile002
 * @tc.desc:  Verify if can get exist profile
 * @tc.type: FUNC
 * @tc.require: AR000GH56I
 */
HWTEST_F(ParseUtilTest, GetProfile002, TestSize.Level1)
{
    DTEST_LOG << " GetProfile002 start " << std::endl;
    /**
     * @tc.steps: step1. check exsit config file
     * @tc.expected: step1. return true when load not exist file
     */
    bool ret = parser_->ParseSaProfiles(TEST_RESOURCE_PATH + "multi_sa_profile.xml");
    EXPECT_TRUE(ret);
    SaProfile saProfile;
    ret = parser_->GetProfile(9999, saProfile);
    EXPECT_TRUE(ret);
    EXPECT_EQ(saProfile.saId, 9999);
    EXPECT_TRUE(saProfile.runOnCreate);
}
} // namespace SAMGR
} // namespace OHOS
