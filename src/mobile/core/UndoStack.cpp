#include "UndoStack.hpp"

#include "libslic3r/Model.hpp"

#include <cassert>
#include <utility>

namespace Slic3r { namespace Mobile {

UndoStack::Snapshot::Snapshot() = default;
UndoStack::Snapshot::~Snapshot() = default;
UndoStack::Snapshot::Snapshot(Snapshot&&) noexcept = default;
UndoStack::Snapshot& UndoStack::Snapshot::operator=(Snapshot&&) noexcept = default;

UndoStack::UndoStack(std::size_t max_depth) : m_max_depth(max_depth == 0 ? 1 : max_depth) {}
UndoStack::~UndoStack() = default;

void UndoStack::take_snapshot(std::string label, const Model& model)
{
    // Truncate the redo tail: anything after the cursor is now unreachable.
    if (m_cursor + 1 < static_cast<long>(m_stack.size()))
        m_stack.erase(m_stack.begin() + (m_cursor + 1), m_stack.end());

    Snapshot s;
    s.label = std::move(label);
    s.model = std::make_unique<Model>(model);
    m_stack.emplace_back(std::move(s));
    m_cursor = static_cast<long>(m_stack.size()) - 1;

    // Enforce the max depth by dropping the oldest entries; the cursor tracks the
    // remaining tail (it stays at the newly pushed snapshot).
    while (m_stack.size() > m_max_depth) {
        m_stack.pop_front();
        --m_cursor;
    }
    assert(m_cursor >= 0);
}

bool UndoStack::undo(Model& out)
{
    if (! can_undo())
        return false;
    --m_cursor;
    out = *m_stack[static_cast<std::size_t>(m_cursor)].model;
    return true;
}

bool UndoStack::redo(Model& out)
{
    if (! can_redo())
        return false;
    ++m_cursor;
    out = *m_stack[static_cast<std::size_t>(m_cursor)].model;
    return true;
}

bool UndoStack::can_undo() const { return m_cursor > 0; }
bool UndoStack::can_redo() const { return m_cursor >= 0 && m_cursor + 1 < static_cast<long>(m_stack.size()); }

std::string UndoStack::undo_label() const
{
    // The label describing the action that would be undone is the label of the current
    // snapshot: undoing walks back to the one before it.
    return can_undo() ? m_stack[static_cast<std::size_t>(m_cursor)].label : std::string {};
}

std::string UndoStack::redo_label() const
{
    return can_redo() ? m_stack[static_cast<std::size_t>(m_cursor + 1)].label : std::string {};
}

std::vector<UndoStack::SnapshotInfo> UndoStack::snapshots() const
{
    std::vector<SnapshotInfo> out;
    out.reserve(m_stack.size());
    for (std::size_t i = 0; i < m_stack.size(); ++i)
        out.push_back({ m_stack[i].label, static_cast<long>(i) == m_cursor });
    return out;
}

void UndoStack::clear()
{
    m_stack.clear();
    m_cursor = -1;
}

}} // namespace Slic3r::Mobile
