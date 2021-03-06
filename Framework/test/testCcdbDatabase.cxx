// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

///
/// \file   testCcdbDatabase.cxx
/// \author Adam Wegrzynek
/// \author Bartheley von Haller
///

#include <unordered_map>
#include "QualityControl/CcdbDatabase.h"
#include "QualityControl/QcInfoLogger.h"
#include "QualityControl/Version.h"

#define BOOST_TEST_MODULE CcdbDatabase test
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include <TH1F.h>
#include "rapidjson/document.h"
#include "QualityControl/RepoPathUtils.h"
#include "QualityControl/testUtils.h"

namespace utf = boost::unit_test;

namespace o2::quality_control::core
{

namespace
{

using namespace o2::quality_control::core;
using namespace o2::quality_control::repository;
using namespace std;
using namespace rapidjson;

const std::string CCDB_ENDPOINT = "ccdb-test.cern.ch:8080";

/**
 * Fixture for the tests, i.e. code is ran in every test that uses it, i.e. it is like a setup and teardown for tests.
 */
struct test_fixture {
  test_fixture()
  {
    backend = std::make_unique<CcdbDatabase>();
    backend->connect(CCDB_ENDPOINT, "", "", "");
    ILOG(Info) << "*** " << boost::unit_test::framework::current_test_case().p_name << " ***" << ENDM;
  }

  ~test_fixture() = default;

  std::unique_ptr<CcdbDatabase> backend;
  map<string, string> metadata;
};

BOOST_AUTO_TEST_CASE(ccdb_create)
{
  test_fixture f;

  f.backend->truncate("my/task", "*");
}

long oldTimestamp;

BOOST_AUTO_TEST_CASE(ccdb_store)
{
  test_fixture f;

  TH1F* h1 = new TH1F("quarantine", "asdf", 100, 0, 99);
  h1->FillRandom("gaus", 10000);
  shared_ptr<MonitorObject> mo1 = make_shared<MonitorObject>(h1, "my/task", "TST");

  TH1F* h2 = new TH1F("metadata", "asdf", 100, 0, 99);
  shared_ptr<MonitorObject> mo2 = make_shared<MonitorObject>(h2, "my/task", "TST");
  mo2->addMetadata("my_meta", "is_good");

  shared_ptr<QualityObject> qo1 = make_shared<QualityObject>(Quality::Bad, "test-ccdb-check", "TST", "OnAll", vector{ string("input1"), string("input2") });
  shared_ptr<QualityObject> qo2 = make_shared<QualityObject>(Quality::Null, "metadata", "TST", "OnAll", vector{ string("input1") });
  qo2->addMetadata("my_meta", "is_good");

  oldTimestamp = CcdbDatabase::getCurrentTimestamp();
  f.backend->storeMO(mo1);
  f.backend->storeMO(mo2);
  f.backend->storeQO(qo1);
  f.backend->storeQO(qo2);
}

BOOST_AUTO_TEST_CASE(ccdb_store_for_future_tests)
{
  // this test is storing a version of the objects in a different directory.
  // The goal is to keep old versions of the objects, in old formats, for future backward compatibility testing.
  test_fixture f;

  TH1F* h1 = new TH1F("to_be_kept", "asdf", 100, 0, 99);
  h1->FillRandom("gaus", 12345);
  shared_ptr<MonitorObject> mo1 = make_shared<MonitorObject>(h1, "task", "TST_KEEP");
  mo1->addMetadata("Run", o2::quality_control::core::Version::GetQcVersion().getString());
  shared_ptr<QualityObject> qo1 = make_shared<QualityObject>(Quality::Bad, "check", "TST_KEEP", "OnAll", vector{ string("input1"), string("input2") });
  qo1->addMetadata("Run", o2::quality_control::core::Version::GetQcVersion().getString());

  f.backend->storeMO(mo1);
  f.backend->storeQO(qo1);
}

BOOST_AUTO_TEST_CASE(ccdb_retrieve_mo, *utf::depends_on("ccdb_store"))
{
  test_fixture f;
  std::shared_ptr<MonitorObject> mo = f.backend->retrieveMO("qc/TST/my/task", "quarantine");
  BOOST_CHECK_NE(mo, nullptr);
  BOOST_CHECK_EQUAL(mo->getName(), "quarantine");
}

BOOST_AUTO_TEST_CASE(ccdb_retrieve_inexisting_mo)
{
  test_fixture f;

  std::shared_ptr<MonitorObject> mo = f.backend->retrieveMO("non/existing", "object");
  BOOST_CHECK(mo == nullptr);
}

BOOST_AUTO_TEST_CASE(ccdb_retrieve_data_024)
{
  // test whether we can read data from version 0.24
  test_fixture f;
  map<string, string> filter = { { "qc_version", "0.24.0" } };
  long timestamp = CcdbDatabase::getFutureTimestamp(60 * 60 * 24 * 365 * 5); // target 5 years in the future as we store with validity of 10 years

  auto* mo = dynamic_cast<MonitorObject*>(f.backend->retrieveTObject("qc/TST_KEEP/task/to_be_kept", filter, timestamp));
  BOOST_CHECK_NE(mo, nullptr);
  BOOST_CHECK_EQUAL(mo->getName(), "to_be_kept");
  BOOST_CHECK_EQUAL(dynamic_cast<TH1F*>(mo->getObject())->GetEntries(), 12345);

  auto* qo = dynamic_cast<QualityObject*>(f.backend->retrieveTObject("qc/checks/TST_KEEP/check", filter, timestamp));
  BOOST_CHECK_NE(qo, nullptr);
  BOOST_CHECK_EQUAL(qo->getName(), "check");
  BOOST_CHECK_EQUAL(qo->getQuality(), o2::quality_control::core::Quality::Bad);

  auto jsonMO = f.backend->retrieveJson("qc/TST_KEEP/task/to_be_kept", timestamp, filter);
  BOOST_CHECK(!jsonMO.empty());

  auto jsonQO = f.backend->retrieveJson("qc/checks/TST_KEEP/check", timestamp, filter);
  BOOST_CHECK(!jsonQO.empty());
}

BOOST_AUTO_TEST_CASE(ccdb_retrieve_data_026)
{
  // test whether we can read data from version 0.26
  test_fixture f;
  map<string, string> filter = { { "qc_version", "0.26.0" } };
  long timestamp = CcdbDatabase::getFutureTimestamp(60 * 60 * 24 * 365 * 5); // target 5 years in the future as we store with validity of 10 years

  auto* tobj = f.backend->retrieveTObject("qc/TST_KEEP/task/to_be_kept", filter, timestamp);
  BOOST_CHECK_NE(tobj, nullptr);
  BOOST_CHECK_EQUAL(tobj->GetName(), "to_be_kept");
  BOOST_CHECK_EQUAL(dynamic_cast<TH1F*>(tobj)->GetEntries(), 12345);

  std::map<std::string, std::string> headers;
  auto* qo = dynamic_cast<QualityObject*>(f.backend->retrieveTObject("qc/checks/TST_KEEP/check", filter, timestamp, &headers));
  BOOST_CHECK_NE(qo, nullptr);
  BOOST_CHECK_EQUAL(qo->getName(), "check");
  BOOST_CHECK_EQUAL(qo->getQuality(), o2::quality_control::core::Quality::Bad);
  BOOST_CHECK(headers.count("Valid-From") > 0);
  string validityQO = headers["Valid-From"];

  auto qo2 = f.backend->retrieveQO("qc/checks/TST_KEEP/check", std::stol(validityQO));
  string run = qo2->getMetadata("Run");
  cout << "Run : " << run << endl;
  BOOST_CHECK(run == "0.26.0");

  auto jsonMO = f.backend->retrieveJson("qc/TST_KEEP/task/to_be_kept", timestamp, filter);
  BOOST_CHECK(!jsonMO.empty());

  auto jsonQO = f.backend->retrieveJson("qc/checks/TST_KEEP/check", timestamp, filter);
  BOOST_CHECK(!jsonQO.empty());
}

BOOST_AUTO_TEST_CASE(ccdb_retrieve_qo, *utf::depends_on("ccdb_store"))
{
  test_fixture f;
  std::shared_ptr<QualityObject> qo = f.backend->retrieveQO(RepoPathUtils::getQoPath("TST", "test-ccdb-check"));
  BOOST_CHECK_NE(qo, nullptr);
  Quality q = qo->getQuality();
  BOOST_CHECK_EQUAL(q.getLevel(), 3);
}

BOOST_AUTO_TEST_CASE(ccdb_retrieve_json, *utf::depends_on("ccdb_store"))
{
  test_fixture f;

  std::string task = "my/task";
  std::string object = "quarantine";
  std::string detector = "TST";

  std::string path = RepoPathUtils::getMoPath(detector, task, object);
  std::cout << "[json retrieve]: " << path << std::endl;
  auto json = f.backend->retrieveJson(path, -1, f.metadata);
  auto json2 = f.backend->retrieveMOJson("qc/TST/" + task, object);

  BOOST_CHECK(!json.empty());
  BOOST_CHECK_EQUAL(json, json2);

  std::string checkName = "test-ccdb-check";
  string qualityPath = RepoPathUtils::getQoPath(detector, checkName);
  std::cout << "[json retrieve]: " << qualityPath << std::endl;
  auto json3 = f.backend->retrieveJson(qualityPath, -1, f.metadata);
  auto json4 = f.backend->retrieveQOJson(qualityPath);
  BOOST_CHECK(!json3.empty());
  BOOST_CHECK_EQUAL(json3, json4);

  Document jsonDocument;
  jsonDocument.Parse(json.c_str());
  BOOST_CHECK(jsonDocument["_typename"].IsString());
  BOOST_CHECK_EQUAL(jsonDocument["_typename"].GetString(), "TH1F");
  BOOST_CHECK(jsonDocument.FindMember("metadata") != jsonDocument.MemberEnd());
  const Value& metadataNode = jsonDocument["metadata"];
  BOOST_CHECK(metadataNode.IsObject());
  BOOST_CHECK(metadataNode.FindMember("qc_task_name") != jsonDocument.MemberEnd());
}

BOOST_AUTO_TEST_CASE(ccdb_metadata, *utf::depends_on("ccdb_store"))
{
  test_fixture f;

  std::string task = "my/task";
  std::string detector = "TST";
  std::string pathQuarantine = RepoPathUtils::getMoPath(detector, task, "quarantine");
  std::string pathMetadata = RepoPathUtils::getMoPath(detector, task, "metadata");
  std::string pathQuality = RepoPathUtils::getQoPath(detector, "test-ccdb-check");
  std::string pathQualityMetadata = RepoPathUtils::getQoPath(detector, "metadata");

  std::map<std::string, std::string> headers1;
  std::map<std::string, std::string> headers2;
  TObject* obj1 = f.backend->retrieveTObject(pathQuarantine, f.metadata, -1, &headers1);
  TObject* obj2 = f.backend->retrieveTObject(pathMetadata, f.metadata, -1, &headers2);
  BOOST_CHECK_NE(obj1, nullptr);
  BOOST_CHECK_NE(obj2, nullptr);
  BOOST_CHECK(headers1.size() > 0);
  BOOST_CHECK(headers2.size() > 1);
  BOOST_CHECK_EQUAL(headers1.count("my_meta"), 0);
  BOOST_CHECK_EQUAL(headers2.count("my_meta"), 1);
  BOOST_CHECK_EQUAL(headers2.at("my_meta"), "is_good");

  auto obj1a = f.backend->retrieveMO("qc/TST/my/task", "quarantine");
  auto obj2a = f.backend->retrieveMO("qc/TST/my/task", "metadata");
  BOOST_CHECK_NE(obj1a, nullptr);
  BOOST_CHECK_NE(obj2a, nullptr);
  BOOST_CHECK(obj1a->getMetadataMap().size() > 0);
  BOOST_CHECK(obj2a->getMetadataMap().size() > 1);
  BOOST_CHECK_EQUAL(obj1a->getMetadataMap().count("my_meta"), 0);
  BOOST_CHECK_EQUAL(obj2a->getMetadataMap().count("my_meta"), 1);
  BOOST_CHECK_EQUAL(obj2a->getMetadataMap().at("my_meta"), "is_good");

  auto obj3 = f.backend->retrieveQO(pathQuality);
  auto obj4 = f.backend->retrieveQO(pathQualityMetadata);
  BOOST_CHECK_NE(obj3, nullptr);
  BOOST_CHECK_NE(obj4, nullptr);
  BOOST_CHECK(obj3->getMetadataMap().size() > 0);
  BOOST_CHECK(obj4->getMetadataMap().size() > 1);
  BOOST_CHECK_EQUAL(obj3->getMetadataMap().count("my_meta"), 0);
  BOOST_CHECK_EQUAL(obj4->getMetadataMap().count("my_meta"), 1);
  BOOST_CHECK_EQUAL(obj4->getMetadataMap().at("my_meta"), "is_good");
}

} // namespace
} // namespace o2::quality_control::core
