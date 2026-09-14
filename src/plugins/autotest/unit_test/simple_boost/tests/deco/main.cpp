#define BOOST_TEST_MODULE Suites and Decorators
#include <boost/test/included/unit_test.hpp>
#include "enab.h"
using boost::unit_test::label;

BOOST_AUTO_TEST_CASE(testWithout1, *label("WO")) { BOOST_TEST (true); }
BOOST_AUTO_TEST_CASE(testWithout2, *label("WO")) { BOOST_TEST (false); }
BOOST_AUTO_TEST_SUITE(SuiteOuter)
  BOOST_AUTO_TEST_SUITE(SuiteInner1)
    BOOST_AUTO_TEST_CASE(Test1) { BOOST_TEST (true); }
    BOOST_AUTO_TEST_CASE(Test2) { BOOST_TEST (true); }
  BOOST_AUTO_TEST_SUITE_END()
  BOOST_AUTO_TEST_SUITE(SuiteInner2, *boost::unit_test::disabled())
    BOOST_AUTO_TEST_CASE(Test1, *label("I2")) { BOOST_TEST (false); }
    BOOST_AUTO_TEST_CASE(Test2, *label("I2")) { BOOST_TEST (false); }
  BOOST_AUTO_TEST_SUITE_END()
  BOOST_AUTO_TEST_CASE(Test1, *label("O1")) { BOOST_TEST (true); }
  BOOST_AUTO_TEST_CASE(Test2) { BOOST_TEST (true); }
  BOOST_AUTO_TEST_CASE(Test2A) { BOOST_TEST (true); }
  // A decorator nobody reads standing in front of one that is read.
  BOOST_AUTO_TEST_CASE(Test3, *label("O3") * boost::unit_test::disabled()) { BOOST_TEST (true); }
BOOST_AUTO_TEST_SUITE_END()

