#include <gtest/gtest.h>
#include <set>
#include <string>
#include "ErrorCode.hpp"
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

/**
 * @brief Assert that a call reported the failure of an unusable identifier.
 *
 * The two functions which read an identifier report every text they refuse with
 * the same code, because a caller cannot act on the reason a text is no
 * identifier: the value is unusable either way.
 *
 * @param[in] result Result of the call to inspect.
 * @return true when the result carries a value, which the caller asserts false.
 */
template <typename T>
bool CarriesValue(const tl::expected<T, appbox::ErrorCode>& result)
{
    EXPECT_FALSE(result.has_value());
    if (result.has_value())
    {
        return true;
    }

    EXPECT_EQ(result.error(), appbox::ErrorCode::kInvalidArgument);
    return false;
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
 *        one, so a caller never has to know which of the two it received.
 */
TEST(Unit_SandboxId, NormalizeAcceptsBothSpellings)
{
    const std::string canonical = "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f";

    const auto digits = appbox::NormalizeSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0f");
    ASSERT_TRUE(digits.has_value());
    EXPECT_EQ(digits.value(), canonical);

    const auto spelled = appbox::NormalizeSandboxId(canonical);
    ASSERT_TRUE(spelled.has_value());
    EXPECT_EQ(spelled.value(), canonical);

    const auto upper_digits = appbox::NormalizeSandboxId("2B1F7C4E9A3D4F6B8C2E5D7A1B3C9E0F");
    ASSERT_TRUE(upper_digits.has_value());
    EXPECT_EQ(upper_digits.value(), canonical);

    const auto upper_spelled = appbox::NormalizeSandboxId("2B1F7C4E-9A3D-4F6B-8C2E-5D7A1B3C9E0F");
    ASSERT_TRUE(upper_spelled.has_value());
    EXPECT_EQ(upper_spelled.value(), canonical);

    /* A normalized identifier is normalized to itself, so a caller may run the
       call on a value it already normalized. */
    const auto twice = appbox::NormalizeSandboxId(spelled.value());
    ASSERT_TRUE(twice.has_value());
    EXPECT_EQ(twice.value(), canonical);
}

/**
 * @brief A text which is no identifier is refused with the code of an unusable
 *        argument instead of being repaired or reported as an empty value.
 *
 * The code tells "the text is no identifier" from a successful result, which a
 * caller needs because an empty text is a legal input of the call and not a
 * failure of it.
 */
TEST(Unit_SandboxId, NormalizeRefusesTextWhichIsNoId)
{
    CarriesValue(appbox::NormalizeSandboxId(""));

    /* Too few and too many digits for either spelling. */
    CarriesValue(appbox::NormalizeSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0"));
    CarriesValue(appbox::NormalizeSandboxId("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0ff"));

    /* A hyphen the layout does not name, and one digit which is no digit. */
    CarriesValue(appbox::NormalizeSandboxId("2b1f7c4e-9a3d-4f6b8c2e-5d7a1b3c9e0f"));
    CarriesValue(appbox::NormalizeSandboxId("2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0z"));

    /* A text of another alphabet, and a text of a wrong separator. */
    CarriesValue(appbox::NormalizeSandboxId("appbox"));
    CarriesValue(appbox::NormalizeSandboxId("2b1f7c4e_9a3d_4f6b_8c2e_5d7a1b3c9e0f"));
}

/**
 * @brief The pipe name of a sandbox is the prefix of the control pipe and the
 *        identifier itself, hyphens kept.
 */
TEST(Unit_SandboxId, PipeNameJoinsThePrefixAndTheId)
{
    const std::string canonical = "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f";

    const auto name = appbox::SandboxPipeName(canonical);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "\\\\.\\pipe\\appbox_" + canonical);

    const auto generated = appbox::SandboxPipeName(appbox::GenerateSandboxId());
    ASSERT_TRUE(generated.has_value());
    EXPECT_EQ(generated.value().rfind("\\\\.\\pipe\\appbox_", 0), 0u);
}

/**
 * @brief An identifier which is not canonical never becomes a pipe name, and
 *        the call reports why instead of answering an empty name.
 */
TEST(Unit_SandboxId, PipeNameRefusesIdsWhichAreNotCanonical)
{
    CarriesValue(appbox::SandboxPipeName(""));

    /* The 32 digit spelling and the upper case spelling are identifiers, but
       neither of them is canonical: the name of the pipe is derived from the
       canonical text alone, so two spellings never name two pipes. */
    CarriesValue(appbox::SandboxPipeName("2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0f"));
    CarriesValue(appbox::SandboxPipeName("2B1F7C4E-9A3D-4F6B-8C2E-5D7A1B3C9E0F"));

    CarriesValue(appbox::SandboxPipeName("appbox"));
}
