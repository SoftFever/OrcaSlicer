#ifndef slic3r_ObjectID_hpp_
#define slic3r_ObjectID_hpp_

#include <cereal/access.hpp>

namespace Slic3r {

namespace UndoRedo {
	class StackImpl;
};

// Unique identifier of a mutable object accross the application.
// Used to synchronize the front end (UI) with the back end (BackgroundSlicingProcess / Print / PrintObject)
// (for Model, ModelObject, ModelVolume, ModelInstance or ModelMaterial classes)
// and to serialize / deserialize an object onto the Undo / Redo stack.
// Valid IDs are strictly positive (non zero).
// It is declared as an object, as some compilers (notably msvcc) consider a typedef size_t equivalent to size_t
// for parameter overload.
class ObjectID
{
public:
	ObjectID(size_t id) : id(id) {}
	// Default constructor constructs an invalid ObjectID.
	ObjectID() : id(0) {}

	bool operator==(const ObjectID &rhs) const { return this->id == rhs.id; }
	bool operator!=(const ObjectID &rhs) const { return this->id != rhs.id; }
	bool operator< (const ObjectID &rhs) const { return this->id <  rhs.id; }
	bool operator> (const ObjectID &rhs) const { return this->id >  rhs.id; }
	bool operator<=(const ObjectID &rhs) const { return this->id <= rhs.id; }
	bool operator>=(const ObjectID &rhs) const { return this->id >= rhs.id; }

    bool valid() const { return id != 0; }
    bool invalid() const { return id == 0; }

	size_t	id;

private:
	friend class cereal::access;
	template<class Archive> void serialize(Archive &ar) { ar(id); }
};

// Base for Model, ModelObject, ModelVolume, ModelInstance or ModelMaterial to provide a unique ID
// to synchronize the front end (UI) with the back end (BackgroundSlicingProcess / Print / PrintObject).
// Also base for Print, PrintObject, SLAPrint, SLAPrintObject to provide a unique ID for matching Model / ModelObject
// with their corresponding Print / PrintObject objects by the notification center at the UI when processing back-end warnings.
// Achtung! The s_last_id counter is not thread safe, so it is expected, that the ObjectBase derived instances
// are only instantiated from the main thread.
class ObjectBase
{
public:
    using Timestamp = uint64_t;

    ObjectID     		id() const { return m_id; }
    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    virtual Timestamp	timestamp() const { return 0; }

protected:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    ObjectBase() : m_id(generate_new_id()) {}
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    ObjectBase(int) : m_id(ObjectID(0)) {}
	// The class tree will have virtual tables and type information.
	virtual ~ObjectBase() = default;

    // Use with caution!
    void        set_new_unique_id() { m_id = generate_new_id(); }
    void        set_invalid_id()    { m_id = 0; }
    // Use with caution!
    void        copy_id(const ObjectBase &rhs) { m_id = rhs.id(); }

    // Override this method if a ObjectBase derived class owns other ObjectBase derived instances.
    virtual void assign_new_unique_ids_recursive() { this->set_new_unique_id(); }

private:
    ObjectID                m_id;

	static inline ObjectID  generate_new_id() { return ObjectID(++ s_last_id); }
    static size_t           s_last_id;
	
	friend ObjectID wipe_tower_object_id();
	friend ObjectID wipe_tower_instance_id();

	friend class cereal::access;
	friend class Slic3r::UndoRedo::StackImpl;
	template<class Archive> void serialize(Archive &ar) { ar(m_id); }
    ObjectBase(const ObjectID id) : m_id(id) {}
  	template<class Archive> static void load_and_construct(Archive & ar, cereal::construct<ObjectBase> &construct) { ObjectID id; ar(id); construct(id); }
};

class ObjectWithTimestamp : public ObjectBase
{
protected:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a new timestamp unique to this object's history.
	ObjectWithTimestamp() = default;
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    ObjectWithTimestamp(int) : ObjectBase(-1) {}
	// The class tree will have virtual tables and type information.
	virtual ~ObjectWithTimestamp() = default;

    // Resetting timestamp to 1 indicates the object is in its initial (cleared) state.
    // To be called by the derived class's clear() method.
    void                reset_timestamp() { m_timestamp = 1; }

public:
    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    Timestamp	        timestamp() const throw() override { return m_timestamp; }
    bool 				timestamp_matches(const ObjectWithTimestamp &rhs) const throw() { return m_timestamp == rhs.m_timestamp; }
    bool 				object_id_and_timestamp_match(const ObjectWithTimestamp &rhs) const throw() { return this->id() == rhs.id() && m_timestamp == rhs.m_timestamp; }
    void 				touch() { m_timestamp = ++ s_last_timestamp; }

private:
	// The first timestamp is non-zero, as zero timestamp means the timestamp is not reliable.
	Timestamp 			m_timestamp { 1 };
    static Timestamp    s_last_timestamp;
	
	friend class cereal::access;
	friend class Slic3r::UndoRedo::StackImpl;
	template<class Archive> void serialize(Archive &ar) { ar(m_timestamp); }
};

// Unique object / instance ID for the wipe tower.
extern ObjectID wipe_tower_object_id();
extern ObjectID wipe_tower_instance_id();

} // namespace Slic3r

#endif /* slic3r_ObjectID_hpp_ */
