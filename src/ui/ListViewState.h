#pragma once

#include <stddef.h>

// Small shared list viewport helper. selected is always the logical item;
// scrollOffset is only the first item currently rendered.
struct ListViewState
{
    size_t selected = 0;
    size_t scrollOffset = 0;

    void ensureVisible(size_t itemCount, size_t visibleRows)
    {
        if (itemCount == 0 || visibleRows == 0)
        {
            selected = 0;
            scrollOffset = 0;
            return;
        }
        if (selected >= itemCount) selected = itemCount - 1;
        if (selected < scrollOffset) scrollOffset = selected;
        if (selected >= scrollOffset + visibleRows)
            scrollOffset = selected - visibleRows + 1;
        const size_t lastOffset = itemCount > visibleRows ? itemCount - visibleRows : 0;
        if (scrollOffset > lastOffset) scrollOffset = lastOffset;
    }

    bool move(int direction, size_t itemCount, size_t visibleRows)
    {
        if (itemCount == 0) { selected = 0; scrollOffset = 0; return false; }
        const size_t previous = selected;
        selected = direction > 0 ? (selected + 1) % itemCount :
            (selected + itemCount - 1) % itemCount;
        ensureVisible(itemCount, visibleRows);
        return selected != previous;
    }
};
