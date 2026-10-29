#include <gtest/gtest.h>
#include <set>
#include <string>
#include "SandboxId.hpp"

namespace
{

/**
 * @brief Whether a text carries only the characters of the canonical form.
 *
 * The helper repeats the layout of the identifier instead of calling
 * `appbox::IsValidSandboxId()`, so a case which checks the validation itself
 * does not lean on the function it checks.
 *
 * @param[in] text Text to inspect.
 * @return true when the text is 36 characters with four hyphens and lower case
 *         hexadecimal digits.
 */
bool HasCanonicalShape(const std::string& text)
{
    if (text.size() != 36)
    {
        return false;
    }

    for (std::size_t position = 0; position < text.size(); ++position)
    {
        const bool hyphen_expected = position == 8 || position == 13 || position == 18 || position == 23;
        const char character = text[position];

        if (hyphen_expected)
        {
            if (character != '-')
            {
                return false;
            }
            continue;
        }

        const bool lower_hex = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        if (!lower_hex)
        {
            return false;
        }
    }

    return true;
}

} // namespace

/**
 * @brief A generated identifier is the text form of a random UUID: 36
 *        characters, the hyphens of the layout and lower case digits only.
 */
TEST(Unit_SandboxId, GenerateNamesTheCanonicalForm)
{
    for (int index = 0; index < 32; ++index)
    {
        const auto id = appbox::GenerateSandboxId();

        EXPECT_TRUE(HasCanonicalShape(id)) << id;
        EXPECT_TRUE(appbox::IsValidSandboxId(id)) << id;
    }
}

/**
 * @brief The generated identifier carries the version and the variant of
 *        RFC 4122: a random UUID is version 4 and the variant octet starts
 *        with the bits `10`.
 */
TEST(Unit_SandboxId, GenerateWritesTheVersionAndTheVariant)
{
    for (int index = 0; index < 32; ++index)
    {
        const auto id = appbox::GenerateSandboxId();

        /* The digits are counted without the hyphens of the layout. */
        ASSERT_EQ(id.size(), 36u) << id;

        /* The thirteenth digit is the first of the third group. */
        EXPECT_EQ(id[14], '4') << id;

        /* The seventeenth digit is the first of the fourth group. */
        const auto variant = id[19];
        EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b') << id;
    }
}

/**
 * @brief Two generated identifiers differ, so two sessions of the packer never
 *        share the pipe name of one sandbox.
 */
TEST(Unit_SandboxId, GenerateProducesDistinctIds)
{
    std::set<std::string> ids;
    for (int index = 0; index < 64; ++index)
    {
        ids.insert(appbox::GenerateSandboxId());
    }

    EXPECT_EQ(ids.size(), 64u);
}

/**
 * @brief The canonical form is accepted, and only it: a text of another length,
 *        another spelling or another character set is rejected.
 */
TEST(Unit_SandboxId, ValidateAcceptsTheCanonicalFormOnly)
{
    EXPECT_TRUE(appbox::IsValidSandboxId("2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f"));

    /* Another length. */
    EXPECT_FALSE(appbox::IsValidSandboxId(""));
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0f"));
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f-"));
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4-9a3d-4f6b-8c2e-5d7a1b3c9e0f"));

    /* Upper case digits, which the canonical form does not carry. */
    EXPECT_FALSE(appbox::IsValidSandboxId("2B1F7C4E-9A3D-4F6B-8C2E-5D7A1B3C9E0F"));

    /* A hyphen the layout does not name, and a missing one. */
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e9-a3d-4f6b-8c2e-5d7a1b3c9e0f"));
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e-9a3d-4f6b8-c2e-5d7a1b3c9e0f"));

    /* Characters which are no hexadecimal digits. */
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0g"));
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0 "));
    EXPECT_FALSE(appbox::IsValidSandboxId("2b1f7c4e_9a3d_4f6b_8c2e_5d7a1b3c9e0f"));
}

/**
 * @brief Both spellings a document may carry are normalized to the canonical
 *        one, and a text which is no identifier at all is refused instead of
 *        being repaired.
 */
TEST(Unit_SandboxId, NormalizeAcceptsBothSpellings)
{
    const std::string canonical = "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f";

    EXPECT_EQ(appbox::NormalizeSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0f"), canonical);
    EXPECT_EQ(appbox::NormalizeSandboxId(canonical), canonical);
    EXPECT_EQ(appbox::NormalizeSandboxId("2B1F7C4E9A3D4F6B8C2E5D7A1B3C9E0F"), canonical);
    EXPECT_EQ(appbox::NormalizeSandboxId("2B1F7C4E-9A3D-4F6B-8C2E-5D7A1B3C9E0F"), canonical);

    /* A normalized identifier is normalized to itself, so a caller may run the
       call on a value it already normalized. */
    EXPECT_EQ(appbox::NormalizeSandboxId(appbox::NormalizeSandboxId(canonical)), canonical);

    /* Text which is no identifier. */
    EXPECT_TRUE(appbox::NormalizeSandboxId("").empty());
    EXPECT_TRUE(appbox::NormalizeSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0").empty());
    EXPECT_TRUE(appbox::NormalizeSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0ff").empty());
    EXPECT_TRUE(appbox::NormalizeSandboxId("2b1f7c4e-9a3d-4f6b8c2e-5d7a1b3c9e0f").empty());
    EXPECT_TRUE(appbox::NormalizeSandboxId("2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0z").empty());
    EXPECT_TRUE(appbox::NormalizeSandboxId("appbox").empty());
}

/**
 * @brief The pipe name of a sandbox is the prefix of the control pipe and the
 *        identifier itself, hyphens kept.
 */
TEST(Unit_SandboxId, PipeNameJoinsThePrefixAndTheId)
{
    const std::string canonical = "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f";

    EXPECT_EQ(appbox::SandboxPipeName(canonical), "\\\\.\\pipe\\appbox_" + canonical);
    EXPECT_EQ(appbox::SandboxPipeName(appbox::GenerateSandboxId()).rfind("\\\\.\\pipe\\appbox_", 0), 0u);

    /* An identifier which is not canonical never becomes a pipe name. */
    EXPECT_TRUE(appbox::SandboxPipeName("").empty());
    EXPECT_TRUE(appbox::SandboxPipeName("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0f").empty());
    EXPECT_TRUE(appbox::SandboxPipeName("2B1F7C4E-9A3D-4F6B-8C2E-5D7A1B3C9E0F").empty());
    EXPECT_TRUE(appbox::SandboxPipeName("appbox").empty());
}
