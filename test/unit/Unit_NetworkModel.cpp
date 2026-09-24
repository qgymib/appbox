#include <gtest/gtest.h>
#include "src/core/NetworkModel.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Add a DNS redirection and fail the test when the model refuses it.
 * @param[in,out] model Model to update.
 * @param[in] hostname Hostname of the entry.
 * @param[in] redirect Redirect target of the entry.
 */
void AddEntry(appbox::NetworkModel& model, const std::wstring& hostname, const std::wstring& redirect)
{
    std::string error;
    ASSERT_TRUE(model.AddDnsEntry(hostname, redirect, error)) << error;
}

/**
 * @brief Take a copy of the entries of a model.
 * @param[in] model Model to read.
 * @return The entries of the model.
 */
std::vector<appbox::DnsRedirectEntry> Snapshot(const appbox::NetworkModel& model)
{
    return model.DnsEntries();
}

/**
 * @brief Compare the entries of a model with a snapshot.
 * @param[in] model Model to read.
 * @param[in] expected Entries the model is expected to hold.
 */
void ExpectEntries(const appbox::NetworkModel& model, const std::vector<appbox::DnsRedirectEntry>& expected)
{
    const auto& entries = model.DnsEntries();
    ASSERT_EQ(entries.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(entries[index].hostname, expected[index].hostname) << "entry " << index;
        EXPECT_EQ(entries[index].redirect, expected[index].redirect) << "entry " << index;
    }
}

} // namespace

TEST(UnitNetworkModel, StartsEmpty)
{
    appbox::NetworkModel model;

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(model.DnsEntries().empty());
    EXPECT_EQ(model.IndexOfHostname(L"example.com"), -1);
}

TEST(UnitNetworkModel, AddsEntriesInInsertionOrder)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");
    AddEntry(model, L"192.168.0.1", L"192.168.0.2");

    EXPECT_FALSE(model.IsEmpty());
    ExpectEntries(model, {
                             { L"example.com",     L"127.0.0.1"   },
                             { L"api.example.com", L"10.0.0.1"    },
                             { L"192.168.0.1",     L"192.168.0.2" }
    });
}

TEST(UnitNetworkModel, IndexOfHostnameIgnoresTheCase)
{
    appbox::NetworkModel model;
    AddEntry(model, L"Example.COM", L"127.0.0.1");

    EXPECT_EQ(model.IndexOfHostname(L"example.com"), 0);
    EXPECT_EQ(model.IndexOfHostname(L"EXAMPLE.COM"), 0);
    EXPECT_EQ(model.IndexOfHostname(L"other.example.com"), -1);
}

TEST(UnitNetworkModel, RefusesEmptyFields)
{
    appbox::NetworkModel model;
    std::string          error;

    EXPECT_FALSE(model.AddDnsEntry(L"", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"", error));
    EXPECT_FALSE(error.empty());

    EXPECT_TRUE(model.IsEmpty());
}

TEST(UnitNetworkModel, RefusesWhitespaceInFields)
{
    appbox::NetworkModel model;
    std::string          error;

    EXPECT_FALSE(model.AddDnsEntry(L"   ", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"\t", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example .com", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"127.0.0.1 ", error));
    EXPECT_FALSE(error.empty());

    EXPECT_TRUE(model.IsEmpty());
}

TEST(UnitNetworkModel, RefusesADuplicateHostnameIgnoringTheCase)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.AddDnsEntry(L"EXAMPLE.COM", L"127.0.0.2", error));
    EXPECT_FALSE(error.empty());
    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(UnitNetworkModel, ARefusedAddLeavesTheModelUntouched)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    const auto before = Snapshot(model);

    std::string error;
    EXPECT_FALSE(model.AddDnsEntry(L"", L"10.0.0.1", error));
    EXPECT_FALSE(model.AddDnsEntry(L"api.example.com", L" ", error));
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"10.0.0.1", error));

    ExpectEntries(model, before);
}

TEST(UnitNetworkModel, SetReplacesTheEntryInPlace)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");

    std::string error;
    ASSERT_TRUE(model.SetDnsEntry(0, L"www.example.com", L"127.0.0.9", error)) << error;

    ExpectEntries(model, {
                             { L"www.example.com", L"127.0.0.9" },
                             { L"api.example.com", L"10.0.0.1"  }
    });
}

TEST(UnitNetworkModel, SetAllowsRecasingItsOwnHostname)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    ASSERT_TRUE(model.SetDnsEntry(0, L"EXAMPLE.COM", L"127.0.0.1", error)) << error;
    ExpectEntries(model, {
                             { L"EXAMPLE.COM", L"127.0.0.1" }
    });
}

TEST(UnitNetworkModel, SetRefusesTheHostnameOfAnotherEntry)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(1, L"Example.com", L"10.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"API.EXAMPLE.COM", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com",     L"127.0.0.1" },
                             { L"api.example.com", L"10.0.0.1"  }
    });
}

TEST(UnitNetworkModel, SetRefusesEmptyAndWhitespaceFields)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(0, L"", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"example.com", L"", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"exam ple.com", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"example.com", L"127.0.0.1\n", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(UnitNetworkModel, SetRefusesAnIndexOutsideTheModel)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(1, L"api.example.com", L"10.0.0.1", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(UnitNetworkModel, RemoveDropsTheEntryAtTheIndex)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");
    AddEntry(model, L"192.168.0.1", L"192.168.0.2");

    ASSERT_TRUE(model.RemoveDnsEntry(1));

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1"   },
                             { L"192.168.0.1", L"192.168.0.2" }
    });
}

TEST(UnitNetworkModel, RemoveRefusesAnIndexOutsideTheModel)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    EXPECT_FALSE(model.RemoveDnsEntry(1));
    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(UnitNetworkModel, ResetDropsEveryEntry)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");

    model.Reset();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(model.DnsEntries().empty());
}
