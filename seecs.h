#ifndef SEECS_ECS_H
#define SEECS_ECS_H

#include <array>
#include <vector>
#include <unordered_map>
#include <limits>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <bitset>
#include <memory>
#include <type_traits>
#include <functional>
#include <typeinfo>

// Can replace these defines with custom macros elsewhere
#ifndef SEECS_ASSERT
	#define SEECS_ASSERT(condition, msg) \
		if (!(condition)) { \
			std::cerr << "[SEECS error]: " << msg << std::endl; \
			::abort(); \
		}
#endif
#ifndef SEECS_INFO
	#ifdef SEECS_INFO_ENABLED
		#define SEECS_INFO(msg) std::cout << "[SEECS info]: " << msg << "\n";
	#else
		#define SEECS_INFO(msg);
	#endif
#endif
#ifndef SEECS_MSG
	#define SEECS_MSG(msg) std::cout << "[SEECS]: " << msg << "\n";
#endif

namespace seecs {

	template <typename... Components>
	class SimpleView;
	class ECS;

	// EntityID uses the first N bits for versioning, and any subsequent bits for the actual entity ID index.
	// - EntityID = [ EntityVersion | EntityIndex ]
	using EntityID = std::uint64_t;
	using EntityVersion = std::uint16_t;
	using EntityIndex = std::uint64_t;

	constexpr std::uint32_t ENTITY_ID_BITS = sizeof(EntityID) * 8;
	constexpr std::uint32_t ENTITY_VERSION_BITS = 12;
	constexpr std::uint32_t ENTITY_INDEX_BITS = ENTITY_ID_BITS - ENTITY_VERSION_BITS;

	static_assert(ENTITY_ID_BITS == sizeof(EntityID) * 8);
	static_assert(ENTITY_VERSION_BITS <= sizeof(EntityVersion) * 8);
	static_assert(ENTITY_INDEX_BITS <= sizeof(EntityIndex) * 8);

	constexpr EntityID ENTITY_INDEX_MASK =
		(static_cast<EntityID>(1) << ENTITY_INDEX_BITS) - 1;

	constexpr EntityID ENTITY_VERSION_MASK = ~ENTITY_INDEX_MASK;

	constexpr EntityVersion MAX_VERSION_VALUE = 
		(static_cast<EntityVersion>(1) << ENTITY_VERSION_BITS) - 1;

	constexpr EntityID NULL_ENTITY_ID = std::numeric_limits<EntityID>::max();
 
	//
	// Handle class; trivially copyable since it just holds an ID that encodes version/index.
	//
	struct Entity {
		constexpr Entity() = default;

		EntityID id() const {
			return m_id;
		}

		constexpr EntityIndex index() const {
			return static_cast<EntityIndex>(m_id & ENTITY_INDEX_MASK);
		}

		constexpr EntityVersion version() const {
			return static_cast<EntityVersion>(
				(m_id & ENTITY_VERSION_MASK) >> ENTITY_INDEX_BITS
				);
		}

		constexpr bool is_null() const {
			return m_id == NULL_ENTITY_ID;
		}

		constexpr explicit operator bool() const {
			return !is_null();
		}

		friend constexpr bool operator==(Entity lhs, Entity rhs) {
			return lhs.m_id == rhs.m_id;
		}

		friend constexpr bool operator!=(Entity lhs, Entity rhs) {
			return !(lhs == rhs);
		}

	private:
		friend class ECS;

		template <typename... Components>
		friend class SimpleView;

		EntityID m_id{ NULL_ENTITY_ID };

		constexpr Entity(EntityVersion version, EntityIndex index)
			: m_id(encode(version, index))
		{}

		static constexpr EntityID encode(EntityVersion version, EntityIndex index) {
			return ((static_cast<EntityID>(version) << ENTITY_INDEX_BITS) & ENTITY_VERSION_MASK) | 
				   (static_cast<EntityID>(index) & ENTITY_INDEX_MASK);
		}
	};


	// Max amount of entities alive at once.
	// - Entity.id() should not exceed this number
	constexpr size_t MAX_ENTITIES = (static_cast<size_t>(1) << ENTITY_INDEX_BITS) - 1;


	// Should be a multiple of 32 (4 bytes), since
	// bitset overallocates by 4 bytes each time.
	constexpr size_t MAX_COMPONENTS = 64;


	//
	// Base class allows runtime polymorphism
	//
	class ISparseSet {
	public:
		virtual ~ISparseSet() = default;
		virtual void unset(EntityIndex) = 0;
		virtual void clear() = 0;
		virtual size_t size() const = 0;
		virtual bool contains_index(EntityIndex id) const = 0;
		virtual std::vector<EntityIndex> get_index_list() const = 0;
	};


	//
	// Basic compile time indexed-type container, associates a type with an index:
	// 
	// Usage: 
	//		using TypeList = type_list<A, B, C>;
	// 
	//		TypeList::get<0> -> A
	//		TypeList::get<1> -> B
	//		...
	//
	template <class... Types>
	struct type_list {
		using type_tuple = std::tuple<Types...>;

		template <size_t Index>
		using get = std::tuple_element_t<Index, type_tuple>;

		static constexpr size_t size = sizeof...(Types);
	};



	//
	// A templated sparse set implementation, mapping EntityIndex -> T
	// 
	// It uses a sparse list and a dense list, allowing us to store tightly
	// packed sequential data that is indexed via sparse indices
	// 
	// It does this via the following relationship: m_dense[m_sparse[EntityIndex]] == component(EntityIndex)
	// 
	// The dense list is tightly packed since we want data close together as it's often accessed sequentially
	// 
	// The sparse list often contains gaps, sacrificing data locality for O(1) lookups
	//
	template <typename T>
	class SparseSet: public ISparseSet {
	private:

		static constexpr size_t tombstone = std::numeric_limits<size_t>::max();

		// Stores index into the m_dense array, where: (m_dense[m_sparse[EntityIndex]] == T)
		std::vector<size_t> m_sparse; 

		// Holds actual (component) data, tightly packed
		std::vector<T> m_dense;

		// 1:1 vector where dense index == (Entity) Index
		// - Allows us to query EntityIndex for dense elements
		std::vector<EntityIndex> m_dense_to_entity_index; 

		//
		// Inserts an index into the sparse list that maps to the dense list,
		// associating m_dense[m_sparse[entity_index]] == m_dense[dense_index]
		//
		inline void set_sparse_index(EntityIndex entity_index, size_t dense_index) {
			if (entity_index >= m_sparse.size())
				m_sparse.resize(entity_index + 1, tombstone);
			m_sparse[entity_index] = dense_index;
		}

		//
		// Returns the dense index for a given EntityIndex,
		// or a tombstone (null) value if non-existent
		//
		inline size_t get_dense_index(EntityIndex entity_index) const {
			if (entity_index >= m_sparse.size())
				return tombstone;

			return m_sparse[entity_index];
		}

	public:

		SparseSet() {
			constexpr size_t BUFFER_ELEMENT_RESERVE_SIZE = 1000; // no. elements
			m_sparse.reserve(BUFFER_ELEMENT_RESERVE_SIZE);
			m_dense.reserve(BUFFER_ELEMENT_RESERVE_SIZE);
			m_dense_to_entity_index.reserve(BUFFER_ELEMENT_RESERVE_SIZE);
		}

		//
		// Upserts into the dense list such that:
		// m_dense[m_sparse[entity_index]] == T
		//
		template <typename U>
		T* set(EntityIndex entity_index, U&& obj) {
			size_t index = get_dense_index(entity_index);

			// Overwrite if data exists at EntityIndex
			if (index != tombstone) {
				m_dense[index] = std::forward<U>(obj);
				return &m_dense[index];
			}

			// Map sparse index to dense index
			set_sparse_index(entity_index, m_dense.size());

			// Push data to the back of the dense list
			m_dense.push_back(std::forward<U>(obj));
			m_dense_to_entity_index.push_back(entity_index);

			return &m_dense.back();
		}

		//
		// Return T* if data exists in dense list, or nullptr if not.
		//
		T* get(EntityIndex entity_index) {
			size_t index = get_dense_index(entity_index);
			return (index != tombstone) ? &m_dense[index] : nullptr;
		}

		//
		// Returns a reference to the data instead of a pointer
		//
		T& get_ref(EntityIndex entity_index) {
			size_t index = get_dense_index(entity_index);
			if (index == tombstone)
				SEECS_ASSERT(false, "get_ref called on invalid EntityIndex " << entity_index);
			return m_dense[index];
		}

		//
		// Removes data from the dense list, and sets the sparse index to a tombstone (null) value
		//
		void unset(EntityIndex entity_index) override {

			size_t deleted_dense_index = get_dense_index(entity_index);

			if (m_dense.empty() || deleted_dense_index == tombstone) return;

			// Swap the back element's index in the dense list to the deleted element's index
			// and sex the deleted element's index to a tombstone (null)
			set_sparse_index(m_dense_to_entity_index.back(), deleted_dense_index);
			set_sparse_index(entity_index, tombstone);

			// Swap and pop: Swap deleted index with back element, then pop off the back of the dense list. 
			// An O(1) operation; at the cost of no strict dense ordering
			std::swap(m_dense.back(), m_dense[deleted_dense_index]);
			std::swap(m_dense_to_entity_index.back(), m_dense_to_entity_index[deleted_dense_index]);

			m_dense.pop_back();
			m_dense_to_entity_index.pop_back();
		}

		size_t size() const override {
			return m_dense.size();
		}

		// 
		// This returns a copy of all EntityIndex that currently have data in
		// the dense list so we can safely delete from the vector while iterating.
		//
		std::vector<EntityIndex> get_index_list() const override {
			return m_dense_to_entity_index;
		}

		//
		// Check if the EntityIndex currently has associated data in the dense list
		//
		bool contains_index(EntityIndex entity_index) const override {
			return get_dense_index(entity_index) != tombstone;
		}

		void clear() override {
			m_dense.clear();
			m_sparse.clear();
			m_dense_to_entity_index.clear();
		}

		bool empty() const {
			return m_dense.empty();
		}

		// 
		// Read-only dense list
		//
		const std::vector<T>& data() const {
			return m_dense;
		}

		void print_dense() const {
			std::stringstream ss;
			std::string delim = "";
			for (const T& e : m_dense) {
				ss << delim << e;
				if (delim.empty())
					delim = ", ";
			}
			SEECS_INFO("[" << ss.str() << "]");
		}

	};

	class ECS {
	private:

		template<typename...>
		friend class SimpleView;

		//
		// Each bit in the mask represents a component type,
		// '1': component exists on entity
		// '0': component doesn't exist on entity
		//
		using ComponentMask = std::bitset<MAX_COMPONENTS>;

		// 
		// MANDATORY info stored per entity.
		//
		struct EntityInfo {
			ComponentMask component_mask{};
			EntityVersion version{ 0 };
		};

		//
		// List of EntityIndexes already created, but no longer in use; to be recycled
		//
		std::vector<EntityIndex> m_available_entities;

		//
		// Maps EntityIndex -> EntityInfo (1:1)
		// - Make sure info here is needed per-entity; if data is sparse, use a SparseSet for that info instead.
		//
		std::vector<EntityInfo> m_entity_info;

		//
		// Associates ID with name provided in create_entity(), mainly for debugging
		// - Keep this as a sparse set since only a few entities might be given a name.
		//
		SparseSet<std::string> m_entity_names;

		//
		// Holds generic pointers to specific component sparse sets.
		// 
		// m_component_pools[get_component_index(Component)] == Pool(Component)
		//
		std::vector<std::unique_ptr<ISparseSet>> m_component_pools;

		//
		// Helpful little vector that associates component type index with
		// a name, so we can retrieve the component name with:
		// 
		// m_component_names[get_component_index(Component)] == Name(Component)
		//
		inline static std::vector<std::string> m_component_names;


		//
		// Highest recorded EntityIndexm should not exceed MAX_ENTITIES
		//
		EntityIndex m_max_entity_index = 0;

		#define SEECS_ASSERT_VALID_ENTITY(entity) \
			SEECS_ASSERT(entity.id() != NULL_ENTITY_ID, "NULL_ENTITY_ID cannot be operated on by the ECS") \
			SEECS_ASSERT(entity.index() < m_entity_info.size(), "Invalid entity index out of bounds: " << entity.id());

		#define SEECS_ASSERT_ALIVE_ENTITY(entity) \
			SEECS_ASSERT(is_alive(entity), "Attempting to access inactive entity: " << entity.id());

	private:

		std::string entity_info_string(Entity entity) {
			std::stringstream ss;

			ss  << "'"
				<< get_entity_name(entity)
				<< "': [INDEX: "
				<< entity.index()
				<< ", VERSION: "
				<< entity.version()
				<< "]";

			return ss.str();
		}

		//
		// Returns the next unused component index.
		//
		// Don't call directly for component type lookup, since each call generates a new
		// index. It is intended to be used only during the first component initialization
		// inside get_component_index
		//
		static size_t next_component_index(std::string type_name) {
			static size_t ind = 0;
			m_component_names.push_back(type_name);
			return ind++;
		};

		//
		// Returns a unique ID for each type, used to index component pools
		// Calling this twice for the same type returns the same index.
		// 
		// This works since it's a static templated function, so the compiler will generate a unique
		// version of this function per type when the program is compiled.
		// Since it's static, all ECS instances share the same index for each component type.
		//
        template <typename T>
        static size_t get_component_index() {
			static size_t ind = next_component_index(typeid(T).name());
            return ind;
        };

		//
		// Same as get_component_index, but will register the component with the ECS if the component doesn't exist yet.
		//
		template <typename T>
		size_t get_or_register_component_index() {
			size_t component_index = get_component_index<T>();

			if (component_index >= m_component_pools.size() || !m_component_pools[component_index])
				register_component<T>();

			// Internal error, should never happen outside development
			SEECS_ASSERT(component_index < m_component_pools.size(),
				"Component index out of bounds for '" << typeid(T).name() << "'");

			return component_index;
		}

		//
		// Retrieves an uncasted pointer to a component pool of type T
		//
		template <typename T>
		ISparseSet* get_component_pool_ptr() {
			size_t index = get_or_register_component_index<T>();
			return m_component_pools[index].get();
		}

		//
		// Retrieves a reference for the specific component pool given the component type
		//
		template <typename T>
		SparseSet<T>& get_component_pool() {
			ISparseSet* base_ptr = get_component_pool_ptr<T>();
			SparseSet<T>* pool = static_cast<SparseSet<T>*>(base_ptr);
			return *pool;
		}

		template <typename Component>
		void set_component_mask_bit(ComponentMask& mask, bool val) {
			size_t bitPos = get_component_index<Component>();
			mask[bitPos] = val;
		}

		template <typename Component>
		bool get_component_mask_bit(ComponentMask& mask) const {
			size_t bitPos = get_component_index<Component>();
			return mask[bitPos];
		}

		ComponentMask& get_entity_component_mask(Entity entity) {
			return m_entity_info[entity.index()].component_mask;
		}

		//
		// Assembles a mask given component types
		// For example: 
		//                                     D C B A
		// generate_component_mask<A,B,D>() -> 1 0 1 1
		//
		template <typename... Components>
		ComponentMask generate_component_mask() {
			ComponentMask mask;
			(set_component_mask_bit<Components>(mask, 1), ...);
			return mask;
		}

		//
		// Wraps the EntityVersion back to 0 once it exceeds MAX_VERSION_VALUE
		//
		EntityVersion next_version(EntityVersion current_version) {
			return static_cast<EntityVersion>((current_version + 1) & MAX_VERSION_VALUE);
		}

	public:

		ECS() = default;

		void reset() {
			m_available_entities.clear();
			m_entity_info.clear();
			m_entity_names.clear();
			m_component_pools.clear();
			m_max_entity_index = 0;
		}

		//
		// Creates an entity and returns and Enttiy handle to the caller.
		//
		// @param(name): Optional and used for debugging purposes
		//
		Entity create_entity(std::string name = "") {
			EntityIndex index{};

			// Either spawn a new EntityIndex, or recycle one if we can
			if (m_available_entities.size() == 0) {
				SEECS_ASSERT(m_max_entity_index < MAX_ENTITIES, "Entity limit exceeded");
				index = m_max_entity_index++;
				m_entity_info.push_back(EntityInfo{});
			}
			else {
				index = m_available_entities.back();
				m_available_entities.pop_back();	
			}

			if (!name.empty())
				m_entity_names.set(index, name);

			Entity entity{ 
				m_entity_info[index].version, 
				index 
			};

			SEECS_INFO("Created: " << entity_info_string(entity));
			return entity;
		}

		std::string get_entity_name(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			std::string* name = m_entity_names.get(entity.index());
			if (name) return *name;

			return "Entity";
		}

		// 
		// Deletes an active entity and its associated components.
		// 
		// Overwrites the given entity to NULL_ENTITY_ID.
		// 
		void delete_entity(Entity& entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			std::string name = get_entity_name(entity);
			ComponentMask& mask = get_entity_component_mask(entity);

			EntityIndex index = entity.index();

			// Fetch this before we delete the entity (only if info is enabled)
			#ifdef SEECS_INFO_ENABLED
				std::string entity_info = entity_info_string(entity);
			#endif

			// Destroy component associations
			for (int i = 0; i < m_component_pools.size(); i++)
				if (mask[i] == 1 && m_component_pools[i])
					m_component_pools[i]->unset(index);

			// Reset entity info and increment version on destruction; allowing us to track active/alive entities.
			m_entity_info[index].component_mask = {};
			m_entity_info[index].version = next_version(m_entity_info[index].version);

			m_entity_names.unset(index);
			m_available_entities.push_back(index);

			entity.m_id = NULL_ENTITY_ID;

			SEECS_INFO("Deleted: " << entity_info);
		}

		//
		// Registers a component and creates a component pool for it
		// 
		// This will be automatically called during runtime, and there's no need to call it explicitly.
		// You can though if you'd like to pre-initialize everything. Not sure if there's a benefit to this though.
		//
		template <typename T>
		void register_component() {
			size_t ind = get_component_index<T>();
			SEECS_ASSERT(ind < MAX_COMPONENTS,
				"Exceeded max number of registered components");

			if (ind >= m_component_pools.size())
				m_component_pools.resize(ind + 1);

			SEECS_ASSERT(!m_component_pools[ind],
				"Attempting to register component '" << typeid(T).name() << "' twice");

			m_component_pools[ind] = std::make_unique<SparseSet<T>>();

			SEECS_INFO("Registered component '" << typeid(T).name() << "'");
		}

		//
		// Attaches a component to an entity
		//
		// Usage: ecs.add<Transform>(player, {x, y, z});
		//
		template <typename T>
		T& add(Entity entity, T&& component = {}) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = get_component_pool<T>();

			EntityIndex index = entity.index();

			// If component already exists, overwrite it's data with new data
			if (pool.get(index))
				return *pool.set(index, std::move(component));

			ComponentMask& mask = get_entity_component_mask(entity);

			set_component_mask_bit<T>(mask, 1);

			SEECS_INFO("Attached '" << typeid(T).name() << "' to " << entity_info_string(entity));
			return *pool.set(index, std::move(component));
		}

		//
		// Retrieves a reference to the specified component for the given entity
		//
		// Usage: Transform& transform = ecs.get<Transform>(player);
		//
		template <typename T>
		T& get(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = get_component_pool<T>();
			EntityIndex index = entity.index();
			T* component = pool.get(index);
			SEECS_ASSERT(component,
				entity_info_string(entity) << " missing component in '" << typeid(T).name() << "' pool");

			return *component;
		}

		//
		// Retrieves a pointer to the specified component for the given entity
		//
		// Usage: Transform* transform = ecs.get_ptr<Transform>(player);
		//
		template <typename T>
		T* get_ptr(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = get_component_pool<T>();
			return pool.get(entity.index());
		}

		//
		// Removes a component from an entity
		//
		// Usage: ecs.remove<Transform>(player);
		//
		template <typename T>
		void remove(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = get_component_pool<T>();
			EntityIndex index = entity.index();

			if (!pool.get(index)) return;

			ComponentMask& mask = get_entity_component_mask(entity);
			set_component_mask_bit<T>(mask, 0);

			pool.unset(index);
			SEECS_INFO("Removed '" << typeid(T).name() << "' from " << entity_info_string(entity));
		}

		//
		// Returns true if the entity has ALL the specified component(s) attached to it
		//
		template <typename... Ts>
		bool has(Entity entity) {
			if (!is_alive(entity))
				return false;

			auto& mask = get_entity_component_mask(entity);
			return (get_component_mask_bit<Ts>(mask) && ...);
		}

		//
		// Returns true if the entity has at least one of the specified components attached to it
		//
		template <typename... Ts>
		bool has_any(Entity entity) {
			return (has<Ts>(entity) || ...);
		}

		//
		//  Create a SimpleView instance which you can iterate via .for_each()
		//
		//  Usage: auto view = ecs.view<A, B>();
		//         view.for_each(...) OR view.packed()
		//
		template <typename... Components>
		SimpleView<Components...> view() {
			return SimpleView<Components...>{ this };
		}

		//
		// Queries whether the entity is currently 'alive' and can be operated on by the ECS.
		// 
		// It does this by comparing the version encoded in the Entity ID with the latest recorded
		// version stored in m_entity_info
		//
		bool is_alive(Entity entity) const {
			if (entity.id() == NULL_ENTITY_ID || entity.index() >= m_entity_info.size())
				return false;

			return m_entity_info[entity.index()].version == entity.version();
		}

		size_t pool_count() const {
			return m_component_pools.size();
		}

		void print_entity_components(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			std::stringstream ss;
			std::string prefix = "";
			ss << entity_info_string(entity) << " components: ";
			ComponentMask& mask = get_entity_component_mask(entity);
			for (int i = 0; i < MAX_COMPONENTS; i++)
				if (mask[i] == 1) {
					ss << prefix << m_component_names[i];
					prefix = ", ";
				}
			
			SEECS_MSG(ss.str());
		}

	};

	//
	// A SimpleView is a basic implementation of a view, allowing iteration based
	// on the passed in Component parameter pack.
	// 
	// This allows us to operate on entities with the given components using lambdas.
	// 
	// This type is ephemeral, and not meant to be persistently stored.
	//
	template <typename... Components>
	class SimpleView {
	private:

		//
		// Gives each component in Components an associated index which we can index
		//
		using ComponentTypes = type_list<Components...>;

		ECS* m_ecs;

		// Stores component pools associated with the view via their component index from ComponentTypes
		std::array<ISparseSet*, sizeof...(Components)> m_view_pools;

		std::vector<ISparseSet*> m_excluded_pools;

		//
		// Pointer to the smallest component pool that we use as our basis for iteration
		//
		// SimpleView<A, B, C>:
		//		Pool(A) x x x x x x x
		//		Pool(B) x x  <----- Pool B will be used to iterate the view
		//		Pool(C) x x x x x
		//
		ISparseSet* m_smallest = nullptr;

		//
		// Returns true iff all the pools in the view contain the given EntityIndex
		//
		bool all_contain(EntityIndex entity_index) {
			return std::all_of(m_view_pools.begin(), m_view_pools.end(), [entity_index](ISparseSet* pool) {
				return pool->contains_index(entity_index);
			});
		}

		//
		// Returns true iff the EntityIndex does not exist in at least one excluded pool
		//
		bool not_excluded(EntityIndex entity_index) {
			if (m_excluded_pools.empty()) return true;

			return std::none_of(m_excluded_pools.begin(), m_excluded_pools.end(), [entity_index](ISparseSet* pool) {
				return pool->contains_index(entity_index);
			});
		}

		Entity entity_from_index(EntityIndex entity_index) {
			return Entity(
				m_ecs->m_entity_info[entity_index].version,
				entity_index
			);
		}

		//
		//	Retrieve a given component pool at an Index in m_view_pools
		//
		template <size_t Index>
		auto get_pool() {
			using componentType = typename ComponentTypes::template get<Index>;
			return static_cast<SparseSet<componentType>*>(m_view_pools[Index]);
		}

		template <size_t... Indices>
		auto component_tuple(EntityIndex entity_index, std::index_sequence<Indices...>) {
			return std::make_tuple((std::ref(get_pool<Indices>()->get_ref(entity_index)))...);
		}

		/*
		*  Provided the function arguments are valid, this function will iterate over the smallest pool
		*  and run the lambda on all entities that contain all the components in the view.
		*
		*  Note: This is the internal implementation: opt for the more user friendly functional ones in the
		*        public interface.
		*/
		template <typename Func>
		void for_each_impl(Func&& func) {
			constexpr auto inds = std::make_index_sequence<sizeof...(Components)>{};

			// Iterate smallest component pool and compare against other pools in view
			// Note this list is a COPY, allowing safe deletion during iteration.
			for (EntityIndex index : m_smallest->get_index_list()) {
				if (all_contain(index) && not_excluded(index)) {
					
					// This branch is for [](Entity entity, Component& c1, Component& c2);
					// constexpr denotes this is evaluated at compile time, which prunes
					// invalid function call branches before runtime to prevent the
					// typical invoke errors you'd see after building.
					if constexpr (std::is_invocable_v<Func, Entity, Components&...>) {
						Entity entity = entity_from_index(index);
						std::apply(func, std::tuple_cat(std::make_tuple(entity), component_tuple(index, inds)));
					}

					// This branch is for [](Component& c1, Component& c2);
					else if constexpr (std::is_invocable_v<Func, Components&...>) {
						std::apply(func, component_tuple(index, inds));
					}

					else {
						SEECS_ASSERT(false,
							"Bad lambda provided to .for_each(), parameter pack does not match lambda args");
					}
				}
			}
		}

	public:

		using ForEachFunc = std::function<void(Components&...)>;
		using ForEachFuncWithID = std::function<void(Entity, Components&...)>;

		SimpleView(ECS* ecs) :
			m_ecs(ecs), m_view_pools{ ecs->get_component_pool_ptr<Components>()... }
		{
			SEECS_ASSERT(ComponentTypes::size == m_view_pools.size(), "Component type list and pool array size mismatch");

			auto smallest_pool = std::min_element(m_view_pools.begin(), m_view_pools.end(),
				[](ISparseSet* a, ISparseSet* b) { return a->size() < b->size(); }
			);

			SEECS_ASSERT(smallest_pool != m_view_pools.end(), "Initializing invalid/empty view");

			m_smallest = *smallest_pool;
		}

		//
		// Specify what component should be excluded from this view.
		// 
		// For example:
		//		ecs.view<A>().without<B, C>();
		// Returns a view that operates on entities that have component A, but do NOT have components B or C
		//
		template <typename... ExcludedComponents>
		SimpleView& without() {
			m_excluded_pools = { m_ecs->get_component_pool_ptr<ExcludedComponents>()... };
			return *this;
		}

		//
		// Executes a passed lambda on all the entities that match the
		// passed parameter pack.
		//
		// Provided function should follow one of two forms:
		// [](Component& c1, Component& c2);
		// OR
		// [](Entity entity, Component& c1, Component& c2);
		//
		void for_each(ForEachFunc func) {
			for_each_impl(func);
		}

		void for_each(ForEachFuncWithID func) {
			for_each_impl(func);
		}

		//
		//	Holds { Entity, ...&components } returned by the view; computed on call instead of
		//  while iterating like .for_each() does.
		// 
		//	Access components that are part of a pack like such:
		//	- auto& [componentA, componentB] = pack.components;
		//
		struct Pack {
			Entity entity;
			std::tuple<Components&...> components;
		};

		// 
		//  Useful when you want a way to iterate a view via indices.
		// 
		//  Usage:
		//		auto packed = ecs.view<A, B>().packed();
		//		for (size_t i = 0; i < packed.size(); i++) {
		// 			auto& [a1, b1] = packed[i].components;
		//		}
		// 
		std::vector<Pack> packed() {
			constexpr auto inds = std::make_index_sequence<sizeof...(Components)>{};
			std::vector<Pack> result;

			for (EntityIndex index : m_smallest->get_index_list())
				if (all_contain(index) && not_excluded(index)) {
					result.push_back({ entity_from_index(index), component_tuple(index, inds)});
				}
			return result;
		}


	};

}

#endif