#include "UndoRedo.hpp"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <typeinfo> 
#include <cassert>
#include <cstddef>

#include <cereal/types/polymorphic.hpp>
#include <cereal/types/map.hpp> 
#include <cereal/types/string.hpp> 
#include <cereal/types/utility.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>
#define CEREAL_FUTURE_EXPERIMENTAL
#include <cereal/archives/adapters.hpp>

#include <libslic3r/ObjectID.hpp>

#include <boost/foreach.hpp>

#ifndef NDEBUG
// #define SLIC3R_UNDOREDO_DEBUG
#endif /* NDEBUG */

namespace Slic3r {
namespace UndoRedo {

// Time interval, start is closed, end is open.
struct Interval
{
public:
	Interval(size_t begin, size_t end) : m_begin(begin), m_end(end) {}

	size_t  begin() const { return m_begin; }
	size_t  end()   const { return m_end; }

	bool 	is_valid() const { return m_begin >= 0 && m_begin < m_end; }
	// This interval comes strictly before the rhs interval.
	bool 	strictly_before(const Interval &rhs) const { return this->is_valid() && rhs.is_valid() && m_end <= rhs.m_begin; }
	// This interval comes strictly after the rhs interval.
	bool 	strictly_after(const Interval &rhs) const { return this->is_valid() && rhs.is_valid() && rhs.m_end <= m_begin; }

	bool    operator<(const Interval &rhs) const { return (m_begin < rhs.m_begin) || (m_begin == rhs.m_begin && m_end < rhs.m_end); }
	bool 	operator==(const Interval &rhs) const { return m_begin == rhs.m_begin && m_end == rhs.m_end; }

	void    trim_end(size_t new_end) { m_end = std::min(m_end, new_end); }
	void 	extend_end(size_t new_end) { assert(new_end >= m_end); m_end = new_end; }

private:
	size_t 	m_begin;
	size_t 	m_end;
};

// History of a single object tracked by the Undo / Redo stack. The object may be mutable or immutable.
class ObjectHistoryBase
{
public:
	virtual ~ObjectHistoryBase() {}

	// Is the object captured by this history mutable or immutable?
	virtual bool is_mutable() const = 0;
	virtual bool is_immutable() const = 0;

	// If the history is empty, the ObjectHistory object could be released.
	virtual bool empty() = 0;

	// Release all data after the given timestamp. For the ImmutableObjectHistory, the shared pointer is NOT released.
	virtual void relese_after_timestamp(size_t timestamp) = 0;

#ifdef SLIC3R_UNDOREDO_DEBUG
	// Human readable debug information.
	virtual std::string	format() = 0;
#endif /* SLIC3R_UNDOREDO_DEBUG */

#ifndef NDEBUG
	virtual bool valid() = 0;
#endif /* NDEBUG */
};

template<typename T> class ObjectHistory : public ObjectHistoryBase
{
public:
	~ObjectHistory() override {}

	// If the history is empty, the ObjectHistory object could be released.
	bool empty() override { return m_history.empty(); }

	// Release all data after the given timestamp. The shared pointer is NOT released.
	void relese_after_timestamp(size_t timestamp) override {
		assert(! m_history.empty());
		assert(this->valid());
		// it points to an interval which either starts with timestamp, or follows the timestamp.
		auto it = std::lower_bound(m_history.begin(), m_history.end(), T(timestamp, timestamp));
		if (it == m_history.end()) {
			auto it_prev = it;
			-- it_prev;
			assert(it_prev->begin() < timestamp);
			// Trim the last interval with timestamp.
			it_prev->trim_end(timestamp);
		}
		m_history.erase(it, m_history.end());
		assert(this->valid());
	}

protected:
	std::vector<T>	m_history;
};

// Big objects (mainly the triangle meshes) are tracked by Slicer using the shared pointers
// and they are immutable.
// The Undo / Redo stack therefore may keep a shared pointer to these immutable objects
// and as long as the ref counter of these objects is higher than 1 (1 reference is held
// by the Undo / Redo stack), there is no cost associated to holding the object
// at the Undo / Redo stack. Once the reference counter drops to 1 (only the Undo / Redo
// stack holds the reference), the shared pointer may get serialized (and possibly compressed)
// and the shared pointer may be released.
// The history of a single immutable object may not be continuous, as an immutable object may
// be removed from the scene while being kept at the Copy / Paste stack.
template<typename T>
class ImmutableObjectHistory : public ObjectHistory<Interval>
{
public:
	ImmutableObjectHistory(std::shared_ptr<const T>	shared_object) : m_shared_object(shared_object) {}
	~ImmutableObjectHistory() override {}

	bool is_mutable() const override { return false; }
	bool is_immutable() const override { return true; }

	void save(size_t active_snapshot_time, size_t current_time) {
		assert(m_history.empty() || m_history.back().end() <= active_snapshot_time || 
			// The snapshot of an immutable object may have already been taken from another mutable object.
			(m_history.back().begin() <= active_snapshot_time && m_history.back().end() == current_time + 1));
		if (m_history.empty() || m_history.back().end() < active_snapshot_time)
			m_history.emplace_back(active_snapshot_time, current_time + 1);
		else
			m_history.back().extend_end(current_time + 1);
	}

	bool has_snapshot(size_t timestamp) {
		if (m_history.empty())
			return false;
		auto it = std::lower_bound(m_history.begin(), m_history.end(), Interval(timestamp, timestamp));
		if (it == m_history.end() || it->begin() > timestamp) {
			if (it == m_history.begin())
				return false;
			-- it;
		}
		return timestamp >= it->begin() && timestamp < it->end();
	}

	bool 						is_serialized() const { return m_shared_object.get() == nullptr; }
	const std::string&			serialized_data() const { return m_serialized; }
	std::shared_ptr<const T>& 	shared_ptr(StackImpl &stack);

#ifdef SLIC3R_UNDOREDO_DEBUG
	std::string 				format() override {
		std::string out = typeid(T).name();
		out += this->is_serialized() ? 
			std::string(" len:") + std::to_string(m_serialized.size()) : 
			std::string(" ptr:") + ptr_to_string(m_shared_object.get());
		for (const Interval &interval : m_history)
			out += std::string(",<") + std::to_string(interval.begin()) + "," + std::to_string(interval.end()) + ")";
		return out;
	}
#endif /* SLIC3R_UNDOREDO_DEBUG */

#ifndef NDEBUG
	bool 						valid() override;
#endif /* NDEBUG */

private:
	// Either the source object is held by a shared pointer and the m_serialized field is empty,
	// or the shared pointer is null and the object is being serialized into m_serialized.
	std::shared_ptr<const T>	m_shared_object;
	std::string 				m_serialized;
};

struct MutableHistoryInterval
{
private:
	struct Data
	{
		// Reference counter of this data chunk. We may have used shared_ptr, but the shared_ptr is thread safe
		// with the associated cost of CPU cache invalidation on refcount change.
		size_t		refcnt;
		size_t		size;
		char 		data[1];

		bool 		matches(const std::string& rhs) { return this->size == rhs.size() && memcmp(this->data, rhs.data(), this->size) == 0; }
	};

	Interval    m_interval;
	Data	   *m_data;

public:
	MutableHistoryInterval(const Interval &interval, const std::string &input_data) : m_interval(interval), m_data(nullptr) {
		m_data = (Data*)new char[offsetof(Data, data) + input_data.size()];
		m_data->refcnt = 1;
		m_data->size = input_data.size();
		memcpy(m_data->data, input_data.data(), input_data.size());
	}

	MutableHistoryInterval(const Interval &interval, MutableHistoryInterval &other) : m_interval(interval), m_data(other.m_data) {
		++ m_data->refcnt;
	}

	// as a key for std::lower_bound
	MutableHistoryInterval(const size_t begin, const size_t end) : m_interval(begin, end), m_data(nullptr) {}

	MutableHistoryInterval(MutableHistoryInterval&& rhs) : m_interval(rhs.m_interval), m_data(rhs.m_data) { rhs.m_data = nullptr; }
	MutableHistoryInterval& operator=(MutableHistoryInterval&& rhs) { m_interval = rhs.m_interval; m_data = rhs.m_data; rhs.m_data = nullptr; return *this; }

	~MutableHistoryInterval() {
		if (m_data != nullptr && -- m_data->refcnt == 0)
			delete[] (char*)m_data;
	}

	const Interval& interval() const { return m_interval; }
	size_t		begin() const { return m_interval.begin(); }
	size_t		end()   const { return m_interval.end(); }
	void 		trim_end(size_t timestamp) { m_interval.trim_end(timestamp); }
	void 		extend_end(size_t timestamp) { m_interval.extend_end(timestamp); }

	bool		operator<(const MutableHistoryInterval& rhs) const { return m_interval < rhs.m_interval; }
	bool 		operator==(const MutableHistoryInterval& rhs) const { return m_interval == rhs.m_interval; }

	const char* data() const { return m_data->data; }
	size_t  	size() const { return m_data->size; }
	size_t		refcnt() const { return m_data->refcnt; }
	bool		matches(const std::string& data) { return m_data->matches(data); }

private:
	MutableHistoryInterval(const MutableHistoryInterval &rhs);
	MutableHistoryInterval& operator=(const MutableHistoryInterval &rhs);
};

static inline std::string ptr_to_string(const void* ptr)
{
	char buf[64];
	sprintf(buf, "%p", ptr);
	return buf;
}

// Smaller objects (Model, ModelObject, ModelInstance, ModelVolume, DynamicPrintConfig)
// are mutable and there is not tracking of the changes, therefore a snapshot needs to be
// taken every time and compared to the previous data at the Undo / Redo stack.
// The serialized data is stored if it is different from the last value on the stack, otherwise
// the serialized data is discarded.
// The history of a single mutable object may not be continuous, as an mutable object may
// be removed from the scene while being kept at the Copy / Paste stack, therefore an object snapshot
// with the same serialized object data may be shared by multiple history intervals.
template<typename T>
class MutableObjectHistory : public ObjectHistory<MutableHistoryInterval>
{
public:
	~MutableObjectHistory() override {}

	bool is_mutable() const override { return true; }
	bool is_immutable() const override { return false; }

	void save(size_t active_snapshot_time, size_t current_time, const std::string &data) {
		assert(m_history.empty() || m_history.back().end() <= active_snapshot_time);
		if (m_history.empty() || m_history.back().end() < active_snapshot_time) {
			if (! m_history.empty() && m_history.back().matches(data))
				// Share the previous data by reference counting.
				m_history.emplace_back(Interval(current_time, current_time + 1), m_history.back());
			else
				// Allocate new data.
				m_history.emplace_back(Interval(current_time, current_time + 1), data);
		} else {
			assert(! m_history.empty());
			assert(m_history.back().end() == active_snapshot_time);
			if (m_history.back().matches(data))
				// Just extend the last interval using the old data.
				m_history.back().extend_end(current_time + 1);
			else
				// Allocate new data time continuous with the previous data.
				m_history.emplace_back(Interval(active_snapshot_time, current_time + 1), data);
		}
	}

	std::string load(size_t timestamp) const {
		assert(! m_history.empty());
		auto it = std::lower_bound(m_history.begin(), m_history.end(), MutableHistoryInterval(timestamp, timestamp));
		if (it == m_history.end() || it->begin() > timestamp) {
			assert(it != m_history.begin());
			-- it;
		}
		assert(timestamp >= it->begin() && timestamp < it->end());
		return std::string(it->data(), it->data() + it->size());
	}

#ifdef SLIC3R_UNDOREDO_DEBUG
	std::string format() override {
		std::string out = typeid(T).name();
		bool first = true;
		for (const MutableHistoryInterval &interval : m_history) {
			if (! first)
				out += ",";
			out += std::string("ptr:") + ptr_to_string(interval.data()) + " len:" + std::to_string(interval.size()) + " <" + std::to_string(interval.begin()) + "," + std::to_string(interval.end()) + ")";
			first = false;
		}
		return out;
	}
#endif /* SLIC3R_UNDOREDO_DEBUG */

#ifndef NDEBUG
	bool valid() override;
#endif /* NDEBUG */
};

#ifndef NDEBUG
template<typename T>
bool ImmutableObjectHistory<T>::valid()
{
	// The immutable object content is captured either by a shared object, or by its serialization, but not both.
	assert(! m_shared_object == ! m_serialized.empty());
	// Verify that the history intervals are sorted and do not overlap.
	if (! m_history.empty())
		for (size_t i = 1; i < m_history.size(); ++ i)
			assert(m_history[i - 1].strictly_before(m_history[i]));
	return true;
}
#endif /* NDEBUG */

#ifndef NDEBUG
template<typename T>
bool MutableObjectHistory<T>::valid()
{
	// Verify that the history intervals are sorted and do not overlap, and that the data reference counters are correct.
	if (! m_history.empty()) {
		std::map<const char*, size_t> refcntrs;
		assert(m_history.front().data() != nullptr);
		++ refcntrs[m_history.front().data()];
		for (size_t i = 1; i < m_history.size(); ++ i) {
			assert(m_history[i - 1].interval().strictly_before(m_history[i].interval()));
			++ refcntrs[m_history[i].data()];
		}
		for (const auto &hi : m_history) {
			assert(hi.data() != nullptr);
			assert(refcntrs[hi.data()] == hi.refcnt());
		}
	}
	return true;
}
#endif /* NDEBUG */

class StackImpl
{
public:
	// Stack needs to be initialized. An empty stack is not valid, there must be a "New Project" status stored at the beginning.
	StackImpl() : m_active_snapshot_time(0), m_current_time(0) {}

	// The Undo / Redo stack is being initialized with an empty model and an empty selection.
	// The first snapshot cannot be removed.
	void initialize(const Slic3r::Model &model, const Slic3r::GUI::Selection &selection);

	// Store the current application state onto the Undo / Redo stack, remove all snapshots after m_active_snapshot_time.
	void take_snapshot(const std::string &snapshot_name, const Slic3r::Model &model, const Slic3r::GUI::Selection &selection);
	void load_snapshot(size_t timestamp, Slic3r::Model &model);

	bool has_undo_snapshot() const;
	bool has_redo_snapshot() const;
	bool undo(Slic3r::Model &model, const Slic3r::GUI::Selection &selection);
	bool redo(Slic3r::Model &model);

	// Snapshot history (names with timestamps).
	const std::vector<Snapshot>& snapshots() const { return m_snapshots; }

	const Selection& selection_deserialized() const { return m_selection; }

//protected:
	template<typename T, typename T_AS> ObjectID save_mutable_object(const T &object);
	template<typename T> ObjectID save_immutable_object(std::shared_ptr<const T> &object);
	template<typename T> T* load_mutable_object(const Slic3r::ObjectID id);
	template<typename T> std::shared_ptr<const T> load_immutable_object(const Slic3r::ObjectID id);
	template<typename T, typename T_AS> void load_mutable_object(const Slic3r::ObjectID id, T &target);

#ifdef SLIC3R_UNDOREDO_DEBUG
	std::string format() const {
		std::string out = "Objects\n";
		for (const std::pair<const ObjectID, std::unique_ptr<ObjectHistoryBase>> &kvp : m_objects)
			out += std::string("ObjectID:") + std::to_string(kvp.first.id) + " " + kvp.second->format() + "\n";
		out += "Snapshots\n";
		for (const Snapshot &snapshot : m_snapshots) {
			if (snapshot.timestamp == m_active_snapshot_time)
				out += ">>> ";
			out += std::string("Name:") + snapshot.name + ", timestamp: " + std::to_string(snapshot.timestamp) + ", Model ID:" + std::to_string(snapshot.model_id) + "\n";
		}
		if (m_active_snapshot_time > m_snapshots.back().timestamp)
			out += ">>>\n";
		out += "Current time: " + std::to_string(m_current_time) + "\n";
		return out;
	}
	void print() const {
		std::cout << "Undo / Redo stack" << std::endl;
		std::cout << this->format() << std::endl;
	}
#endif /* SLIC3R_UNDOREDO_DEBUG */


#ifndef NDEBUG
	bool valid() const {
		assert(! m_snapshots.empty());
		auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
		assert(it == m_snapshots.end() || (it != m_snapshots.begin() && it->timestamp == m_active_snapshot_time));
		assert(it != m_snapshots.end() || m_active_snapshot_time > m_snapshots.back().timestamp);
		for (auto it = m_objects.begin(); it != m_objects.end(); ++ it) 
			assert(it->second->valid());
		return true;
	}
#endif /* NDEBUG */

private:
	template<typename T> ObjectID 	immutable_object_id(const std::shared_ptr<const T> &ptr) { 
		return this->immutable_object_id_impl((const void*)ptr.get());
	}
	ObjectID     					immutable_object_id_impl(const void *ptr) {
		auto it = m_shared_ptr_to_object_id.find(ptr);
		if (it == m_shared_ptr_to_object_id.end()) {
			// Allocate a new temporary ObjectID for this shared pointer.
			ObjectBase object_with_id;
			it = m_shared_ptr_to_object_id.insert(it, std::make_pair(ptr, object_with_id.id()));
		}
		return it->second;
	}
	void 							collect_garbage();

	// Each individual object (Model, ModelObject, ModelInstance, ModelVolume, Selection, TriangleMesh)
	// is stored with its own history, referenced by the ObjectID. Immutable objects do not provide
	// their own IDs, therefore there are temporary IDs generated for them and stored to m_shared_ptr_to_object_id.
	std::map<ObjectID, std::unique_ptr<ObjectHistoryBase>> 	m_objects;
	std::map<const void*, ObjectID>							m_shared_ptr_to_object_id;
	// Snapshot history (names with timestamps).
	std::vector<Snapshot>									m_snapshots;
	// Timestamp of the active snapshot.
	size_t 													m_active_snapshot_time;
	// Logical time counter. m_current_time is being incremented with each snapshot taken.
	size_t 													m_current_time;
	// Last selection serialized or deserialized.
	Selection 												m_selection;
};

using InputArchive  = cereal::UserDataAdapter<StackImpl, cereal::BinaryInputArchive>;
using OutputArchive = cereal::UserDataAdapter<StackImpl, cereal::BinaryOutputArchive>;

} // namespace UndoRedo

class Model;
class ModelObject;
class ModelVolume;
class ModelInstance;
class ModelMaterial;
class ModelConfig;
class DynamicPrintConfig;
class TriangleMesh;

} // namespace Slic3r

namespace cereal
{
	// Let cereal know that there are load / save non-member functions declared for ModelObject*, ignore serialization of pointers triggering 
	// static assert, that cereal does not support serialization of raw pointers.
	template <class Archive> struct specialize<Archive, Slic3r::Model*, cereal::specialization::non_member_load_save> {};
	template <class Archive> struct specialize<Archive, Slic3r::ModelObject*, cereal::specialization::non_member_load_save> {};
	template <class Archive> struct specialize<Archive, Slic3r::ModelVolume*, cereal::specialization::non_member_load_save> {};
	template <class Archive> struct specialize<Archive, Slic3r::ModelInstance*, cereal::specialization::non_member_load_save> {};
	template <class Archive> struct specialize<Archive, Slic3r::ModelMaterial*, cereal::specialization::non_member_load_save> {};
	template <class Archive> struct specialize<Archive, Slic3r::ModelConfig, cereal::specialization::non_member_load_save> {};
	template <class Archive> struct specialize<Archive, std::shared_ptr<Slic3r::TriangleMesh>, cereal::specialization::non_member_load_save> {};

	// Store ObjectBase derived class onto the Undo / Redo stack as a separate object,
	// store just the ObjectID to this stream.
	template <class T> void save(BinaryOutputArchive& ar, T* const& ptr)
	{
		ar(cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar).save_mutable_object<T, T>(*ptr));
	}

	// Load ObjectBase derived class from the Undo / Redo stack as a separate object
	// based on the ObjectID loaded from this stream.
	template <class T> void load(BinaryInputArchive& ar, T*& ptr)
	{
		Slic3r::UndoRedo::StackImpl& stack = cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar);
		size_t id;
		ar(id);
		ptr = stack.load_mutable_object<T>(Slic3r::ObjectID(id));
	}

	// Store ObjectBase derived class onto the Undo / Redo stack as a separate object,
	// store just the ObjectID to this stream.
	template <class T> void save(BinaryOutputArchive &ar, const std::unique_ptr<T> &ptr)
	{
		ar(cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar).save_mutable_object<T>(*ptr.get()));
	}

	// Load ObjectBase derived class from the Undo / Redo stack as a separate object
	// based on the ObjectID loaded from this stream.
	template <class T> void load(BinaryInputArchive &ar, std::unique_ptr<T> &ptr)
	{
		Slic3r::UndoRedo::StackImpl& stack = cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar);
		size_t id;
		ar(id);
		ptr.reset(stack.load_mutable_object<T>(Slic3r::ObjectID(id)));
	}

	// Store ObjectBase derived class onto the Undo / Redo stack as a separate object,
	// store just the ObjectID to this stream.
	void save(BinaryOutputArchive& ar, const Slic3r::ModelConfig &cfg)
	{
		ar(cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar).save_mutable_object<Slic3r::ModelConfig, Slic3r::DynamicPrintConfig>(cfg));
	}

	// Load ObjectBase derived class from the Undo / Redo stack as a separate object
	// based on the ObjectID loaded from this stream.
	void load(BinaryInputArchive& ar, Slic3r::ModelConfig &cfg)
	{
		Slic3r::UndoRedo::StackImpl& stack = cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar);
		size_t id;
		ar(id);
		stack.load_mutable_object<Slic3r::ModelConfig, Slic3r::DynamicPrintConfig>(Slic3r::ObjectID(id), cfg);
	}

	// Store ObjectBase derived class onto the Undo / Redo stack as a separate object,
	// store just the ObjectID to this stream.
	template <class T> void save(BinaryOutputArchive &ar, const std::shared_ptr<const T> &ptr)
	{
		ar(cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar).save_immutable_object<T>(const_cast<std::shared_ptr<const T>&>(ptr)));
	}

	// Load ObjectBase derived class from the Undo / Redo stack as a separate object
	// based on the ObjectID loaded from this stream.
	template <class T> void load(BinaryInputArchive &ar, std::shared_ptr<const T> &ptr)
	{
		Slic3r::UndoRedo::StackImpl &stack = cereal::get_user_data<Slic3r::UndoRedo::StackImpl>(ar);
		size_t id;
		ar(id);
		ptr = stack.load_immutable_object<T>(Slic3r::ObjectID(id));
	}
}

#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <slic3r/GUI/Selection.hpp>

namespace Slic3r {
namespace UndoRedo {

template<typename T> std::shared_ptr<const T>& 	ImmutableObjectHistory<T>::shared_ptr(StackImpl &stack)
{
	if (m_shared_object.get() == nullptr && ! this->m_serialized.empty()) {
		// Deserialize the object.
		std::istringstream iss(m_serialized);
		{
			Slic3r::UndoRedo::InputArchive archive(stack, iss);
			std::unique_ptr<std::remove_const<T>::type> mesh(new std::remove_const<T>::type());
			archive(*mesh.get());
			m_shared_object = std::move(mesh);
		}
	}
	return m_shared_object;
}

template<typename T, typename T_AS> ObjectID StackImpl::save_mutable_object(const T &object)
{
	// First find or allocate a history stack for the ObjectID of this object instance.
	auto it_object_history = m_objects.find(object.id());
	if (it_object_history == m_objects.end())
		it_object_history = m_objects.insert(it_object_history, std::make_pair(object.id(), std::unique_ptr<MutableObjectHistory<T>>(new MutableObjectHistory<T>())));
	auto *object_history = static_cast<MutableObjectHistory<T>*>(it_object_history->second.get());
	// Then serialize the object into a string.
	std::ostringstream oss;
	{
		Slic3r::UndoRedo::OutputArchive archive(*this, oss);
		archive(static_cast<const T_AS&>(object));
	}
	object_history->save(m_active_snapshot_time, m_current_time, oss.str());
	return object.id();
}

template<typename T> ObjectID StackImpl::save_immutable_object(std::shared_ptr<const T> &object)
{
	// First allocate a temporary ObjectID for this pointer.
	ObjectID object_id = this->immutable_object_id(object);
	// and find or allocate a history stack for the ObjectID associated to this shared_ptr.
	auto it_object_history = m_objects.find(object_id);
	if (it_object_history == m_objects.end())
		it_object_history = m_objects.emplace_hint(it_object_history, object_id, std::unique_ptr<ImmutableObjectHistory<T>>(new ImmutableObjectHistory<T>(object)));
	// Then save the interval.
	static_cast<ImmutableObjectHistory<T>*>(it_object_history->second.get())->save(m_active_snapshot_time, m_current_time);
	return object_id;
}

template<typename T> T* StackImpl::load_mutable_object(const Slic3r::ObjectID id)
{
	T *target = new T();
	this->load_mutable_object<T, T>(id, *target);
	return target;
}

template<typename T> std::shared_ptr<const T> StackImpl::load_immutable_object(const Slic3r::ObjectID id)
{
	// First find a history stack for the ObjectID of this object instance.
	auto it_object_history = m_objects.find(id);
	assert(it_object_history != m_objects.end());
	auto *object_history = static_cast<ImmutableObjectHistory<T>*>(it_object_history->second.get());
	assert(object_history->has_snapshot(m_active_snapshot_time));
	return object_history->shared_ptr(*this);
}

template<typename T, typename T_AS> void StackImpl::load_mutable_object(const Slic3r::ObjectID id, T &target)
{
	// First find a history stack for the ObjectID of this object instance.
	auto it_object_history = m_objects.find(id);
	assert(it_object_history != m_objects.end());
	auto *object_history = static_cast<const MutableObjectHistory<T>*>(it_object_history->second.get());
	// Then get the data associated with the object history and m_active_snapshot_time.
	std::istringstream iss(object_history->load(m_active_snapshot_time));
	Slic3r::UndoRedo::InputArchive archive(*this, iss);
	target.m_id = id;
	archive(static_cast<T_AS&>(target));
}

// The Undo / Redo stack is being initialized with an empty model and an empty selection.
// The first snapshot cannot be removed.
void StackImpl::initialize(const Slic3r::Model &model, const Slic3r::GUI::Selection &selection)
{
	assert(m_active_snapshot_time == 0);
	assert(m_current_time == 0);
	// The initial time interval will be <0, 1)
	m_active_snapshot_time = SIZE_MAX; // let it overflow to zero in take_snapshot
	m_current_time = 0;
 	this->take_snapshot("Internal - Initialized", model, selection);
}

// Store the current application state onto the Undo / Redo stack, remove all snapshots after m_active_snapshot_time.
void StackImpl::take_snapshot(const std::string &snapshot_name, const Slic3r::Model &model, const Slic3r::GUI::Selection &selection)
{
	// Release old snapshot data.
	// The active snapshot may be above the last snapshot if there is no redo data available.
	if (! m_snapshots.empty() && m_active_snapshot_time > m_snapshots.back().timestamp)
		m_active_snapshot_time = m_snapshots.back().timestamp + 1;
	else
		++ m_active_snapshot_time;
	for (auto &kvp : m_objects)
		kvp.second->relese_after_timestamp(m_active_snapshot_time);
	{
		auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
		m_snapshots.erase(it, m_snapshots.end());
	}
	// Take new snapshots.
	this->save_mutable_object<Slic3r::Model, Slic3r::Model>(model);
	m_selection.volumes_and_instances.clear();
	m_selection.volumes_and_instances.reserve(selection.get_volume_idxs().size());
	m_selection.mode = selection.get_mode();
	for (unsigned int volume_idx : selection.get_volume_idxs())
		m_selection.volumes_and_instances.emplace_back(selection.get_volume(volume_idx)->geometry_id);
	this->save_mutable_object<Selection, Selection>(m_selection);
	// Save the snapshot info.
	m_snapshots.emplace_back(snapshot_name, m_current_time ++, model.id().id);
	m_active_snapshot_time = m_current_time;
	// Release empty objects from the history.
	this->collect_garbage();
	assert(this->valid());
#ifdef SLIC3R_UNDOREDO_DEBUG
	std::cout << "After snapshot" << std::endl;
	this->print();
#endif /* SLIC3R_UNDOREDO_DEBUG */
}

void StackImpl::load_snapshot(size_t timestamp, Slic3r::Model &model)
{
	// Find the snapshot by time. It must exist.
	const auto it_snapshot = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(timestamp));
	if (it_snapshot == m_snapshots.begin() || it_snapshot == m_snapshots.end() || it_snapshot->timestamp != timestamp)
		throw std::runtime_error((boost::format("Snapshot with timestamp %1% does not exist") % timestamp).str());

	m_active_snapshot_time = timestamp;
	model.clear_objects();
	model.clear_materials();
	this->load_mutable_object<Slic3r::Model, Slic3r::Model>(ObjectID(it_snapshot->model_id), model);
	model.update_links_bottom_up_recursive();
	m_selection.volumes_and_instances.clear();
	this->load_mutable_object<Selection, Selection>(m_selection.id(), m_selection);
	// Sort the volumes so that we may use binary search.
	std::sort(m_selection.volumes_and_instances.begin(), m_selection.volumes_and_instances.end());
	this->m_active_snapshot_time = timestamp;
	assert(this->valid());
}

bool StackImpl::has_undo_snapshot() const
{ 
	assert(this->valid());
	auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
	return -- it != m_snapshots.begin();
}

bool StackImpl::has_redo_snapshot() const
{ 
	auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
	return it != m_snapshots.end() && ++ it != m_snapshots.end();
}

bool StackImpl::undo(Slic3r::Model &model, const Slic3r::GUI::Selection &selection)
{ 
	assert(this->valid());
	auto it_current = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
	if (-- it_current == m_snapshots.begin())
		return false;
	this->load_snapshot(it_current->timestamp, model);
#ifdef SLIC3R_UNDOREDO_DEBUG
	std::cout << "After undo" << std::endl;
 	this->print();
#endif /* SLIC3R_UNDOREDO_DEBUG */
	return true;
}

bool StackImpl::redo(Slic3r::Model &model)
{ 
	assert(this->valid());
	auto it_current = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
	if (it_current == m_snapshots.end() || ++ it_current == m_snapshots.end())
		return false;
	this->load_snapshot(it_current->timestamp, model);
#ifdef SLIC3R_UNDOREDO_DEBUG
	std::cout << "After redo" << std::endl;
 	this->print();
#endif /* SLIC3R_UNDOREDO_DEBUG */
	return true;
}

void StackImpl::collect_garbage()
{
	// Purge objects with empty histories.
	for (auto it = m_objects.begin(); it != m_objects.end();) {
		if (it->second->empty()) {
			if (it->second->is_immutable())
				// Release the immutable object from the ptr to ObjectID map.
				this->m_objects.erase(it->first);
			it = m_objects.erase(it);
		} else
			++ it;
	}
}

// Wrappers of the private implementation.
Stack::Stack() : pimpl(new StackImpl()) {}
Stack::~Stack() {}
void Stack::initialize(const Slic3r::Model &model, const Slic3r::GUI::Selection &selection) { pimpl->initialize(model, selection); }
void Stack::take_snapshot(const std::string &snapshot_name, const Slic3r::Model &model, const Slic3r::GUI::Selection &selection) { pimpl->take_snapshot(snapshot_name, model, selection); }
void Stack::load_snapshot(size_t timestamp, Slic3r::Model &model) { pimpl->load_snapshot(timestamp, model); }
bool Stack::has_undo_snapshot() const { return pimpl->has_undo_snapshot(); }
bool Stack::has_redo_snapshot() const { return pimpl->has_redo_snapshot(); }
bool Stack::undo(Slic3r::Model &model, const Slic3r::GUI::Selection &selection) { return pimpl->undo(model, selection); }
bool Stack::redo(Slic3r::Model &model) { return pimpl->redo(model); }
const Selection& Stack::selection_deserialized() const { return pimpl->selection_deserialized(); }

const std::vector<Snapshot>& Stack::snapshots() const { return pimpl->snapshots(); }

} // namespace UndoRedo
} // namespace Slic3r


//FIXME we should have unit tests for testing serialization of basic types as DynamicPrintConfig.
#if 0
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
namespace Slic3r {
	bool test_dynamic_print_config_serialization() {
		FullPrintConfig full_print_config;
		DynamicPrintConfig cfg;
		cfg.apply(full_print_config, false);

		std::string serialized;
	   	try {
			std::ostringstream ss;
			cereal::BinaryOutputArchive oarchive(ss);
	        oarchive(cfg);
			serialized = ss.str();
	    } catch (std::runtime_error e) {
	        e.what();
	    }

	    DynamicPrintConfig cfg2;
	   	try {
			std::stringstream ss(serialized);
			cereal::BinaryInputArchive iarchive(ss);
	        iarchive(cfg2);
	    } catch (std::runtime_error e) {
	        e.what();
	    }

	    if (cfg == cfg2) {
	    	printf("Yes!\n");
			return true;
	    }
	    printf("No!\n");
		return false;
	}
} // namespace Slic3r
#endif
