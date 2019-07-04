#ifndef slic3r_Utils_UndoRedo_hpp_
#define slic3r_Utils_UndoRedo_hpp_

#include <memory>
#include <string>

#include <libslic3r/ObjectID.hpp>

namespace Slic3r {

class Model;

namespace GUI {
	class Selection;
} // namespace GUI

namespace UndoRedo {

struct Snapshot
{
	Snapshot(size_t timestamp) : timestamp(timestamp) {}
	Snapshot(const std::string &name, size_t timestamp, size_t model_object_id) : name(name), timestamp(timestamp), model_object_id(model_object_id) {}
	
	std::string name;
	size_t 		timestamp;
	size_t 		model_object_id;

	bool		operator< (const Snapshot &rhs) const { return this->timestamp < rhs.timestamp; }
	bool		operator==(const Snapshot &rhs) const { return this->timestamp == rhs.timestamp; }
};

// Excerpt of Slic3r::GUI::Selection for serialization onto the Undo / Redo stack.
struct Selection : public Slic3r::ObjectBase {
	unsigned char								mode;
	std::vector<std::pair<ObjectID, ObjectID>>	volumes_and_instances;
	template<class Archive> void serialize(Archive &ar) { ar(mode, volumes_and_instances); }
};

class StackImpl;

class Stack
{
public:
	// Stack needs to be initialized. An empty stack is not valid, there must be a "New Project" status stored at the beginning.
	Stack();
	~Stack();

	// The Undo / Redo stack is being initialized with an empty model and an empty selection.
	// The first snapshot cannot be removed.
	void initialize(const Slic3r::Model &model, const Slic3r::GUI::Selection &selection);

	// Store the current application state onto the Undo / Redo stack, remove all snapshots after m_active_snapshot_time.
	void take_snapshot(const std::string &snapshot_name, const Slic3r::Model &model, const Slic3r::GUI::Selection &selection);
	void load_snapshot(size_t timestamp, Slic3r::Model &model);

	bool undo(Slic3r::Model &model);
	bool redo(Slic3r::Model &model);

	// Snapshot history (names with timestamps).
	const std::vector<Snapshot>& snapshots() const;

	// After load_snapshot() / undo() / redo() the selection is deserialized into a list of ObjectIDs, which needs to be converted
	// into the list of GLVolume pointers once the 3D scene is updated.
	const Selection& selection_deserialized() const;

private:
	friend class StackImpl;
	std::unique_ptr<StackImpl> 	pimpl;
};

}; // namespace UndoRedo
}; // namespace Slic3r

#endif /* slic3r_Utils_UndoRedo_hpp_ */
