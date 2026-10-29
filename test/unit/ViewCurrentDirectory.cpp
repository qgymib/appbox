#include "WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <string>
#include "filesystem/ViewCurrentDirectory.hpp"

namespace
{

using appbox::filesystem::DosPathToNtPath;
using appbox::filesystem::NtPathToDosPath;
using appbox::filesystem::RebaseCurrentDirectory;

/**
 * @brief Root of the upper layer of the case, in the form the layers are named.
 *
 * The upper layer encodes the drive of a view path as the first component below
 * its root, which is the shape the resolver applies and the shape the
 * translation below expects.
 */
constexpr const wchar_t* kUpperLayer = L"\\??\\C:\\sandbox\\data\\filesystem";

/**
 * @brief Whether a path starts with a prefix, ignoring the case of the letters.
 * @param[in] path Path to inspect.
 * @param[in] prefix Prefix to look for.
 * @return true when the path starts with the prefix.
 */
bool StartsWith(const std::wstring& path, const std::wstring& prefix)
{
    if (path.size() < prefix.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index)
    {
        const wchar_t left = path[index];
        const wchar_t right = prefix[index];
        const wchar_t folded_left = (left >= L'A' && left <= L'Z') ? static_cast<wchar_t>(left - L'A' + L'a') : left;
        const wchar_t folded_right =
            (right >= L'A' && right <= L'Z') ? static_cast<wchar_t>(right - L'A' + L'a') : right;
        if (folded_left != folded_right)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Translation of the layers of the case into the view of the case.
 *
 * The translation is the one of the upper layer: a path below
 * `\??\C:\sandbox\data\filesystem\C` is a path of the view `C:\`. A path which
 * belongs to no layer is refused, which is what the sandbox answers as well.
 *
 * @param[in] layer_path Path of a layer.
 * @param[out] view_path Path of the view the layer path belongs to.
 * @return true when the path belongs to a layer of the case.
 */
bool FakeRebase(const std::wstring& layer_path, std::wstring& view_path)
{
    const std::wstring root = std::wstring(kUpperLayer) + L"\\C";
    if (!StartsWith(layer_path, root))
    {
        return false;
    }

    /*
     * The root of the drive of the view is spelled with the separator the view
     * paths below it carry, which is the shape the resolver answers as well.
     */
    const std::wstring remainder = layer_path.substr(root.size());
    view_path = std::wstring(L"\\??\\C:") + (remainder.empty() ? L"\\" : remainder);
    return true;
}

/**
 * @brief Translation which never matches, like a run without layers.
 * @param[in] layer_path Path of a layer.
 * @param[out] view_path Path of the view the layer path belongs to.
 * @return false, so the caller keeps the path it was given.
 */
bool NoRebase(const std::wstring& layer_path, std::wstring& view_path)
{
    (void)layer_path;
    (void)view_path;
    return false;
}

} // namespace

/**
 * @brief The path of a drive and the path of a share are translated into the
 *        namespace of the object manager, and every other shape is refused.
 */
TEST(Unit_ViewCurrentDirectory, DosPathToNtPath)
{
    std::wstring nt;

    /* The path of a drive. */
    ASSERT_TRUE(DosPathToNtPath(L"C:\\dir", nt));
    EXPECT_EQ(nt, L"\\??\\C:\\dir");

    ASSERT_TRUE(DosPathToNtPath(L"c:\\dir\\sub", nt));
    EXPECT_EQ(nt, L"\\??\\c:\\dir\\sub");

    /* The root of a drive, which is the shortest path of a drive. */
    ASSERT_TRUE(DosPathToNtPath(L"D:\\", nt));
    EXPECT_EQ(nt, L"\\??\\D:\\");

    /* The path of a share. */
    ASSERT_TRUE(DosPathToNtPath(L"\\\\server\\share", nt));
    EXPECT_EQ(nt, L"\\??\\UNC\\server\\share");

    ASSERT_TRUE(DosPathToNtPath(L"\\\\server\\share\\dir", nt));
    EXPECT_EQ(nt, L"\\??\\UNC\\server\\share\\dir");

    /* A device, a pipe, a drive relative path and an empty path are no paths of the view. */
    EXPECT_FALSE(DosPathToNtPath(L"", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\\\.\\pipe\\appbox", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\\\?\\C:\\dir", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\\\server", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\\\server\\", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\\\server\\\\share", nt));
    EXPECT_FALSE(DosPathToNtPath(L"C:", nt));
    EXPECT_FALSE(DosPathToNtPath(L"C:dir", nt));
    EXPECT_FALSE(DosPathToNtPath(L"dir", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\dir", nt));

    /* A path which already carries a namespace is no DOS path. */
    EXPECT_FALSE(DosPathToNtPath(L"\\??\\C:\\dir", nt));
    EXPECT_FALSE(DosPathToNtPath(L"\\GLOBAL??\\C:\\dir", nt));
}

/**
 * @brief The namespace of the object manager is dropped, and both spellings of
 *        it are accepted.
 */
TEST(Unit_ViewCurrentDirectory, NtPathToDosPath)
{
    std::wstring dos;

    ASSERT_TRUE(NtPathToDosPath(L"\\??\\C:\\dir", dos));
    EXPECT_EQ(dos, L"C:\\dir");

    ASSERT_TRUE(NtPathToDosPath(L"\\GLOBAL??\\C:\\dir\\sub", dos));
    EXPECT_EQ(dos, L"C:\\dir\\sub");

    ASSERT_TRUE(NtPathToDosPath(L"\\??\\UNC\\server\\share\\dir", dos));
    EXPECT_EQ(dos, L"\\\\server\\share\\dir");

    /* A path without a namespace, a relative path and a nameless share are refused. */
    EXPECT_FALSE(NtPathToDosPath(L"", dos));
    EXPECT_FALSE(NtPathToDosPath(L"C:\\dir", dos));
    EXPECT_FALSE(NtPathToDosPath(L"\\dir", dos));
    EXPECT_FALSE(NtPathToDosPath(L"\\??\\dir", dos));
    EXPECT_FALSE(NtPathToDosPath(L"\\??\\C:relative", dos));
    EXPECT_FALSE(NtPathToDosPath(L"\\??\\UNC\\", dos));
}

/**
 * @brief A directory which belongs to a layer is reported as the path of its
 *        view, and running the call on that path answers the same path: the
 *        path of a view belongs to no layer.
 */
TEST(Unit_ViewCurrentDirectory, RebaseResultIsIdempotent)
{
    std::wstring view;

    ASSERT_TRUE(RebaseCurrentDirectory(L"C:\\sandbox\\data\\filesystem\\C\\dir", FakeRebase, view));
    EXPECT_EQ(view, L"C:\\dir");

    std::wstring again;
    ASSERT_TRUE(RebaseCurrentDirectory(view, FakeRebase, again));
    EXPECT_EQ(again, view);

    /* The same holds for the root of a layer. */
    ASSERT_TRUE(RebaseCurrentDirectory(L"C:\\sandbox\\data\\filesystem\\C", FakeRebase, view));
    EXPECT_EQ(view, L"C:\\");
}

/**
 * @brief A directory which belongs to no layer keeps its path, which is the
 *        state of a run without layers as well.
 */
TEST(Unit_ViewCurrentDirectory, OutsideLayerKeepsThePath)
{
    std::wstring view;

    ASSERT_TRUE(RebaseCurrentDirectory(L"C:\\work", NoRebase, view));
    EXPECT_EQ(view, L"C:\\work");

    ASSERT_TRUE(RebaseCurrentDirectory(L"\\\\server\\share\\dir", NoRebase, view));
    EXPECT_EQ(view, L"\\\\server\\share\\dir");

    /* The root of a drive which the layers do not mount keeps its path as well. */
    ASSERT_TRUE(RebaseCurrentDirectory(L"D:\\", NoRebase, view));
    EXPECT_EQ(view, L"D:\\");
}

/**
 * @brief A directory the view cannot name is not rewritten at all, so the
 *        process keeps the directory it was created with.
 */
TEST(Unit_ViewCurrentDirectory, UnsupportedFormIsNotRewritten)
{
    std::wstring view;

    EXPECT_FALSE(RebaseCurrentDirectory(L"\\\\.\\pipe\\appbox", FakeRebase, view));
    EXPECT_FALSE(RebaseCurrentDirectory(L"dir", FakeRebase, view));
    EXPECT_FALSE(RebaseCurrentDirectory(L"", FakeRebase, view));

    /* A missing translation is a failure as well, not a rewrite to the same path. */
    EXPECT_FALSE(RebaseCurrentDirectory(L"C:\\dir", nullptr, view));
}
