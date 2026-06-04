#include "FileFullDirInformationWalker.hpp"

    // 校验位于 off 处的结构是否完整；完整则返回 (头 + FileName) 的字节数，
    // 否则返回 0 表示截断/越界。
static size_t entry_size(size_t off, size_t size, size_t kHeaderSize, uint8_t* const base)
{
        if (off > size || size - off < kHeaderSize)
        {
            return 0;
        }
        auto* e = reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(base + off);
        // 防止 FileNameLength 异常大导致溢出
        if (e->FileNameLength > size - off - kHeaderSize)
        {
            return 0;
        }
        return kHeaderSize + e->FileNameLength;
}

size_t appbox::FileFullDirInformationWalker::Walk(void* buff, size_t size, const Callback& cb)
{
    if (buff == nullptr || size == 0)
    {
        return 0;
    }

    constexpr size_t kHeaderSize = offsetof(FILE_FULL_DIR_INFORMATION, FileName);

    auto* const base = static_cast<uint8_t*>(buff);

    // ===== 阶段一：处理首结构可能被删除的情况 =====
    // 若首结构被删，则将其后继搬到 buff 起始位置并修复偏移，
    // 然后对新的首结构再次判定（循环）。
    while (true)
    {
        if (entry_size(0, size, kHeaderSize, base) == 0)
        {
            // 首结构本身就不完整：无有效数据
            return 0;
        }

        auto* first = reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(base);
        ULONG next_rel = first->NextEntryOffset;

        // 若声明了下一个结构，但下一个结构截断了，先做保护修复
        if (next_rel != 0)
        {
            if (next_rel < kHeaderSize || // 重叠/非法
                entry_size(next_rel, size, kHeaderSize, base) == 0)
            { // 截断
                first->NextEntryOffset = 0;
                next_rel = 0;
            }
        }

        if (!cb(first))
        {
            break; // 保留首结构，进入阶段二
        }

        // ----- 需要删除首结构 -----
        if (next_rel == 0)
        {
            // 已经是最后一个，删除后整个 buff 为空
            return 0;
        }

        // 仅搬移"下一个"结构本体到偏移 0
        auto*        next = reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(base + next_rel);
        const ULONG  nn_rel = next->NextEntryOffset; // 下下个的相对偏移（基于 next）
        const size_t mov_len = kHeaderSize + next->FileNameLength;

        std::memmove(base, base + next_rel, mov_len);

        auto* moved = reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(base);
        // 下下个结构的物理位置不变：仍在原 next_rel + nn_rel 处。
        // 因 moved 已被搬到偏移 0，故新的相对偏移 = next_rel + nn_rel。
        moved->NextEntryOffset = (nn_rel == 0) ? 0 : (next_rel + nn_rel);

        // 再次循环重判 moved
    }

    // ===== 阶段二：从首结构出发，向后遍历 =====
    size_t prev_off = 0;

    while (true)
    {
        auto*       prev = reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(base + prev_off);
        const ULONG rel = prev->NextEntryOffset;

        if (rel == 0)
        {
            // prev 已是最后一个
            return prev_off + kHeaderSize + prev->FileNameLength;
        }

        const size_t cur_off = prev_off + rel;
        const size_t cur_sz = entry_size(cur_off, size, kHeaderSize, base);
        if (cur_sz == 0)
        {
            // 当前结构截断：丢弃，并保护 prev
            prev->NextEntryOffset = 0;
            return prev_off + kHeaderSize + prev->FileNameLength;
        }

        auto* cur = reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(base + cur_off);
        ULONG cur_rel = cur->NextEntryOffset;

        // 若 cur 之后的结构截断，先做保护
        if (cur_rel != 0)
        {
            if (cur_rel < kHeaderSize || entry_size(cur_off + cur_rel, size, kHeaderSize, base) == 0)
            {
                cur->NextEntryOffset = 0;
                cur_rel = 0;
            }
        }

        if (cb(cur))
        {
            // 删除 cur
            if (cur_rel == 0)
            {
                // cur 是最后一个：缩短有效长度
                prev->NextEntryOffset = 0;
                return prev_off + kHeaderSize + prev->FileNameLength;
            }
            // 中间删除：仅改 prev 的偏移，跳过 cur（保留 cur 字节为"洞"）
            prev->NextEntryOffset = rel + cur_rel;
            // prev_off 不变，下一轮判定新的 cur
        }
        else
        {
            // 保留 cur，前进
            prev_off = cur_off;
        }
    }
}
