#include <gtest/gtest.h>
#include "network/DnsTable.hpp"
#include <string>

using appbox::network::DnsTable;
using appbox::network::RequestedFamily;

namespace
{

/**
 * @brief Parse a document and fail the test when it is refused.
 * @param[in,out] table Table to fill.
 * @param[in] text Document to parse.
 */
void ParseOrFail(DnsTable& table, const std::string& text)
{
    std::string error;
    ASSERT_TRUE(table.Parse(text, error)) << error;
}

} // namespace

TEST(UnitDnsTable, StartsEmpty)
{
    const DnsTable table;

    EXPECT_EQ(table.Count(), 0u);
    EXPECT_EQ(table.Find("example.com", RequestedFamily::Any), nullptr);
}

TEST(UnitDnsTable, ReadsTheEntriesOfADocument)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[
        {"hostname":"example.com","redirect":"127.0.0.1"},
        {"hostname":"api.example.com","redirect":"::1"}]})");

    ASSERT_EQ(table.Count(), 2u);

    const auto* first = table.Find("example.com", RequestedFamily::Any);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->hostname, "example.com");
    EXPECT_EQ(first->redirect, "127.0.0.1");
    EXPECT_EQ(first->family, appbox::network_isolation::AddressFamily::IPv4);

    const auto* second = table.Find("api.example.com", RequestedFamily::Any);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->redirect, "::1");
    EXPECT_EQ(second->family, appbox::network_isolation::AddressFamily::IPv6);
}

TEST(UnitDnsTable, AcceptsADocumentWithoutAnEntryList)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1})");

    EXPECT_EQ(table.Count(), 0u);
}

TEST(UnitDnsTable, LookupIgnoresTheCaseAndATrailingDot)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[{"hostname":"Example.COM","redirect":"127.0.0.1"}]})");

    EXPECT_NE(table.Find("example.com", RequestedFamily::Any), nullptr);
    EXPECT_NE(table.Find("EXAMPLE.COM", RequestedFamily::Any), nullptr);
    EXPECT_NE(table.Find("example.com.", RequestedFamily::Any), nullptr);
    EXPECT_NE(table.Find("Example.Com.", RequestedFamily::Any), nullptr);
    EXPECT_EQ(table.Find("www.example.com", RequestedFamily::Any), nullptr);
    EXPECT_EQ(table.Find("example.org", RequestedFamily::Any), nullptr);
}

TEST(UnitDnsTable, LookupHonoursTheRequestedFamily)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[
        {"hostname":"v4.example.com","redirect":"127.0.0.1"},
        {"hostname":"v6.example.com","redirect":"::1"}]})");

    EXPECT_NE(table.Find("v4.example.com", RequestedFamily::IPv4), nullptr);
    EXPECT_EQ(table.Find("v4.example.com", RequestedFamily::IPv6), nullptr);
    EXPECT_NE(table.Find("v4.example.com", RequestedFamily::Any), nullptr);

    EXPECT_NE(table.Find("v6.example.com", RequestedFamily::IPv6), nullptr);
    EXPECT_EQ(table.Find("v6.example.com", RequestedFamily::IPv4), nullptr);
    EXPECT_NE(table.Find("v6.example.com", RequestedFamily::Any), nullptr);
}

TEST(UnitDnsTable, TheLastEntryOfAHostnameWins)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[
        {"hostname":"example.com","redirect":"127.0.0.1"},
        {"hostname":"EXAMPLE.com","redirect":"10.0.0.1"}]})");

    ASSERT_EQ(table.Count(), 1u);
    const auto* entry = table.Find("example.com", RequestedFamily::Any);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->redirect, "10.0.0.1");
}

TEST(UnitDnsTable, RefusesADocumentOfAnotherVersion)
{
    DnsTable    table;
    std::string error;

    EXPECT_FALSE(table.Parse(R"({"version":2,"entries":[]})", error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(table.Parse(R"({"entries":[]})", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(table.Count(), 0u);
}

TEST(UnitDnsTable, RefusesAMalformedDocument)
{
    DnsTable    table;
    std::string error;

    EXPECT_FALSE(table.Parse("not json", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(table.Parse(R"([1,2,3])", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(table.Parse(R"({"version":1,"entries":{}})", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(table.Parse(R"({"version":1,"entries":[7]})", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(table.Parse(R"({"version":1,"entries":[{"hostname":"example.com"}]})", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(table.Parse(R"({"version":1,"entries":[{"hostname":"","redirect":"127.0.0.1"}]})", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(table.Parse(R"({"version":1,"entries":[{"hostname":"example.com","redirect":"nope"}]})", error));
    EXPECT_FALSE(error.empty());

    EXPECT_EQ(table.Count(), 0u);
}

TEST(UnitDnsTable, ARefusedDocumentLeavesTheTableUntouched)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[{"hostname":"example.com","redirect":"127.0.0.1"}]})");
    ASSERT_EQ(table.Count(), 1u);

    std::string error;
    EXPECT_FALSE(table.Parse(R"({"version":1,"entries":[{"hostname":"api.example.com","redirect":"bad"}]})", error));

    ASSERT_EQ(table.Count(), 1u);
    EXPECT_NE(table.Find("example.com", RequestedFamily::Any), nullptr);
    EXPECT_EQ(table.Find("api.example.com", RequestedFamily::Any), nullptr);
}

TEST(UnitDnsTable, ParsingAgainReplacesTheEntries)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[{"hostname":"example.com","redirect":"127.0.0.1"}]})");
    ParseOrFail(table, R"({"version":1,"entries":[{"hostname":"other.example.com","redirect":"10.0.0.1"}]})");

    EXPECT_EQ(table.Count(), 1u);
    EXPECT_EQ(table.Find("example.com", RequestedFamily::Any), nullptr);
    EXPECT_NE(table.Find("other.example.com", RequestedFamily::Any), nullptr);
}

TEST(UnitDnsTable, ClearDropsEveryEntry)
{
    DnsTable table;
    ParseOrFail(table, R"({"version":1,"entries":[{"hostname":"example.com","redirect":"127.0.0.1"}]})");

    table.Clear();

    EXPECT_EQ(table.Count(), 0u);
    EXPECT_EQ(table.Find("example.com", RequestedFamily::Any), nullptr);
}
