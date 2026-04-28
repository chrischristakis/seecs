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
#include <typeindex>
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

	constexpr std::uint32_t ENTITY_ID_BITS = 64;
	constexpr std::uint32_t ENTITY_VERSION_BITS = 12;
	constexpr std::uint32_t ENTITY_INDEX_BITS = ENTITY_ID_BITS - ENTITY_VERSION_BITS;

	static_assert(ENTITY_ID_BITS == sizeof(EntityID) * 8);
	static_assert(ENTITY_VERSION_BITS <= sizeof(EntityVersion) * 8);
	static_assert(ENTITY_INDEX_BITS <= sizeof(EntityIndex) * 8);

	constexpr EntityID ENTITY_INDEX_MASK =
		(static_cast<EntityID>(1) << ENTITY_INDEX_BITS) - 1;

	constexpr EntityID ENTITY_VERSION_MASK = ~ENTITY_INDEX_MASK;

	constexpr EntityID NULL_ENTITY_ID = std::numeric_limits<EntityID>::max();

	/* 
	 * Handle class; trivially copyable since it just holds an ID that encodes version/index.
	*/
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

		EntityID m_id{ NULL_ENTITY_ID };

		constexpr Entity(EntityVersion version, EntityIndex index)
			: m_id(pack(version, index))
		{}

		static constexpr EntityID pack(EntityVersion version, EntityIndex index) {
			return ((static_cast<EntityID>(version) << ENTITY_INDEX_BITS) & ENTITY_VERSION_MASK)
				| (static_cast<EntityID>(index) & ENTITY_INDEX_MASK);
		}
	};


	// Max amount of entities alive at once.
	// Set this to NULL_ENTITY_ID if you want no limit.
	// Once limit is hit, an assert will fire and
	// the program will terminate.
	constexpr size_t MAX_ENTITIES = (static_cast<size_t>(1) << ENTITY_INDEX_BITS) - 1;


	// Should be a multiple of 32 (4 bytes), since
	// bitset overallocates by 4 bytes each time.
	constexpr size_t MAX_COMPONENTS = 64;


	// Base class allows runtime polymorphism
	class ISparseSet {
	public:
		virtual ~ISparseSet() = default;
		virtual void Delete(EntityIndex) = 0;
		virtual void Clear() = 0;
		virtual size_t Size() const = 0;
		virtual bool ContainsIndex(EntityIndex id) const = 0;
		virtual std::vector<EntityIndex> GetIndexList() const = 0;
	};


	/*
	* Basic type container, can use each type by providing compile-time index:
	* 
	*    using typeContainer = type_list<...>;
	*	 typename typeContainer::template get<0> type;
	*/
	template <class... Types>
	struct type_list {
		using type_tuple = std::tuple<Types...>;

		template <size_t Index>
		using get = std::tuple_element_t<Index, type_tuple>;

		static constexpr size_t size = sizeof...(Types);
	};



	/*
	*  A templated sparse set implementation, mapping EntityID -> T
	* 
	*  - Get(EntityID): returns T or NULL if EntityID is not in sparse set
	*  - Set(EntityID, T&&): Adds/Overwrites into the dense list for the specified entity
	*  - Delete(EntityID): Removes data for EntityID from dense list
	*/
	template <typename T>
	class SparseSet: public ISparseSet {
	private:

		static constexpr size_t SPARSE_MAX_SIZE = 2048; // elements
		static constexpr size_t tombstone = std::numeric_limits<size_t>::max();

		using Sparse = std::array<size_t, SPARSE_MAX_SIZE>;

		std::vector<Sparse> m_sparsePages;

		std::vector<T> m_dense;
		std::vector<EntityIndex> m_denseToIndex; // 1:1 vector where dense index == (Entity) Index

		/*
		* Inserts a given dense index into the sparse vector, associating
		* an Entity ID with the index in the dense vector.
		*
		* This doesnt actually insert anything into the dense
		* vector, it simply defines a mapping from ID -> index
		*/
		inline void SetDenseIndex(EntityIndex id, size_t index) {
			size_t page = id / SPARSE_MAX_SIZE;
			size_t sparseIndex = id % SPARSE_MAX_SIZE; // Index local to a page

			// Fill in new pages with tombstones
			if (page >= m_sparsePages.size()) {
				size_t oldSize = m_sparsePages.size();

				m_sparsePages.resize(page + 1);

				for (size_t i = oldSize; i <= page; i++) 
					m_sparsePages[i].fill(tombstone);
			}

			Sparse& sparse = m_sparsePages[page];

			sparse[sparseIndex] = index;
		}

		/*
		* Returns the dense index for a given entity ID,
		* or a tombstone (null) value if non-existent
		*/
		inline size_t GetDenseIndex(EntityIndex id) const {
			size_t page = id / SPARSE_MAX_SIZE;
			size_t sparseIndex = id % SPARSE_MAX_SIZE;

			if (page < m_sparsePages.size()) {
				const Sparse& sparse = m_sparsePages[page];
				return sparse[sparseIndex];
			}

			return tombstone;
		}

	public:

		SparseSet() {
			// Avoids initial copies/allocation, feel free to alter size
			m_dense.reserve(1000);
			m_denseToIndex.reserve(1000);
		}

		template <typename U>
		T* Set(EntityIndex id, U&& obj) {
			size_t index = GetDenseIndex(id);

			if (index != tombstone) {
				m_dense[index] = std::forward<U>(obj);
				m_denseToIndex[index] = id;
				return &m_dense[index];
			}

			SetDenseIndex(id, m_dense.size());

			m_dense.push_back(std::forward<U>(obj));
			m_denseToIndex.push_back(id);

			return &m_dense.back();
		}

		T* Get(EntityIndex id) {
			size_t index = GetDenseIndex(id);
			return (index != tombstone) ? &m_dense[index] : nullptr;
		}

		T& GetRef(EntityIndex id) {
			size_t index = GetDenseIndex(id);
			if (index == tombstone)
				SEECS_ASSERT(false, "GetRef called on invalid entity with ID " << id);
			return m_dense[index];
		}

		void Delete(EntityIndex id) override {

			size_t deletedIndex = GetDenseIndex(id);

			if (m_dense.empty() || deletedIndex == tombstone) return;

			SetDenseIndex(m_denseToIndex.back(), deletedIndex);
			SetDenseIndex(id, tombstone);

			std::swap(m_dense.back(), m_dense[deletedIndex]);
			std::swap(m_denseToIndex.back(), m_denseToIndex[deletedIndex]);

			m_dense.pop_back();
			m_denseToIndex.pop_back();
		}

		size_t Size() const override {
			return m_dense.size();
		}

		// This returns a copy so we can safely delete while iterating.
		std::vector<EntityIndex> GetIndexList() const override {
			return m_denseToIndex;
		}

		bool ContainsIndex(EntityIndex id) const override {
			return GetDenseIndex(id) != tombstone;
		}

		void Clear() override {
			m_dense.clear();
			m_sparsePages.clear();
			m_denseToIndex.clear();
		}

		bool IsEmpty() const {
			return m_dense.empty();
		}

		// Read-only dense list
		const std::vector<T>& Data() const {
			return m_dense;
		}

		void PrintDense() const {
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

		// Each bit in the mask represents a component,
		// '1' == active, '0' == inactive.
		using ComponentMask = std::bitset<MAX_COMPONENTS>;

		// MANDATORY info stored per entity
		struct EntityInfo {
			ComponentMask componentMask{};
			EntityVersion version{ 0 };
		};

		// List of IDs already created, but no longer in use
		std::vector<EntityIndex> m_availableEntities;

		// Maps Index -> Info
		// - Make sure info here is needed per entity; if data is sparse, use a SparseSet instead.
		std::vector<EntityInfo> m_entityInfo;


		// Associates ID with name provided in CreateEntity(), mainly for debugging
		// - Keep this as a sparse set since only a few entities might be given a name
		SparseSet<std::string> m_entityNames;


		// Holds generic pointers to specific component sparse sets.
		// 
		// Index into this array using the corresponding bit position
		// found by using m_componentBitPosition
		std::vector<std::unique_ptr<ISparseSet>> m_componentPools;


		// Helpful little vector that associates component index with a name
		// Just for debugging.
		inline static std::vector<std::string> m_componentNames;


		// Highest recorded entity ID
		EntityIndex m_maxEntityIndex = 0;

#define SEECS_ASSERT_VALID_ENTITY(entity) \
			SEECS_ASSERT(entity.id() != NULL_ENTITY_ID, "NULL_ENTITY_ID cannot be operated on by the ECS") \
			SEECS_ASSERT(entity.index() < m_entityInfo.size(), "Invalid entity index out of bounds: " << entity.id());

#define SEECS_ASSERT_ALIVE_ENTITY(entity) \
			SEECS_ASSERT(IsAlive(entity), "Attempting to access inactive entity: " << entity.id());

	private:

		std::string GetEntityInfo(Entity entity) {
			std::stringstream ss;

			ss  << "'"
				<< GetEntityName(entity)
				<< "': [INDEX: "
				<< entity.index()
				<< ", VERSION: "
				<< entity.version()
				<< "]";

			return ss.str();
		}

		static size_t GetNextComponentIndex(std::string typeName) {
			static size_t ind = 0;
			m_componentNames.push_back(typeName);
			return ind++;
		};

		// Returns a unique ID for each type, used to index component pools
		// - Since it's static, all ECS instances share the same index for each component type.
        template <typename T>
        static size_t GetComponentIndex() {
			static size_t ind = GetNextComponentIndex(typeid(T).name());
            return ind;
        };

		// Same as GetComponentTypeIndex, but will register if the component doesn't exist yet.
		template <typename T>
		size_t GetOrRegisterComponentIndex() {
			size_t index = GetComponentIndex<T>();

			if (index >= m_componentPools.size() || !m_componentPools[index])
				RegisterComponent<T>();

			// Internal error, should never happen outside development
			SEECS_ASSERT(index < m_componentPools.size() && index >= 0,
				"Type index out of bounds for component '" << typeid(T).name() << "'");

			return index;
		}

		/*
		*   Retrieves an uncasted pointer to a pool of type T
		*/
		template <typename T>
		ISparseSet* GetComponentPoolPtr() {
			size_t index = GetOrRegisterComponentIndex<T>();
			return m_componentPools[index].get();
		}

		/*
		* Retrieves reference for the specific component pool given a component name
		*/
		template <typename T>
		SparseSet<T>& GetComponentPool() {
			ISparseSet* genericPtr = GetComponentPoolPtr<T>();
			SparseSet<T>* pool = static_cast<SparseSet<T>*>(genericPtr);

			return *pool;
		}

		template <typename Component>
		void SetComponentBit(ComponentMask& mask, bool val) {
			size_t bitPos = GetComponentIndex<Component>();
			mask[bitPos] = val;
		}

		template <typename Component>
		bool GetComponentBit(ComponentMask& mask) const {
			size_t bitPos = GetComponentIndex<Component>();
			return mask[bitPos];
		}

		ComponentMask& GetEntityComponentMask(Entity entity) {
			return m_entityInfo[entity.index()].componentMask;
		}

		/*
		*  Assembles a generic mask for the given components
		*/
		template <typename... Components>
		ComponentMask CreateComponentMask() {
			ComponentMask mask;
			(SetComponentBit<Components>(mask, 1), ...);
			return mask;
		}

	public:

		ECS() = default;

		void Reset() {
			m_availableEntities.clear();
			m_entityInfo.clear();
			m_entityNames.Clear();
			m_componentPools.clear();
			m_maxEntityIndex = 0;
		}

		/*
		*  Creates an entity and returns the ID to refer to that entity.
		*
		*  @param(name):
		*  * Optional and used for debugging purposes, it
		*    shouldn't be used often since there's no optimization
		*    in place yet for entities that share a name.
		*/
		Entity CreateEntity(std::string name = "") {
			EntityIndex index{};

			// Either spawn a new ID or recycle one
			if (m_availableEntities.size() == 0) {
				SEECS_ASSERT(m_maxEntityIndex < MAX_ENTITIES, "Entity limit exceeded");
				index = m_maxEntityIndex++;
				m_entityInfo.push_back(EntityInfo{});
			}
			else {
				index = m_availableEntities.back();
				m_availableEntities.pop_back();	
			}

			if (!name.empty())
				m_entityNames.Set(index, name);

			EntityVersion version = m_entityInfo[index].version;

			Entity entity{ version, index };
			SEECS_INFO("Created: " << GetEntityInfo(entity));
			return entity;
		}

		std::string GetEntityName(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			std::string* name = m_entityNames.Get(entity.index());
			if (name) return *name;

			return "Entity";
		}

		/*
		* Deletes an active entity and its associated components.
		* - Overwrites the given entity to NULL_ENTITY_ID.
		*
		*/
		void DeleteEntity(Entity& entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			std::string name = GetEntityName(entity);
			ComponentMask& mask = GetEntityComponentMask(entity);

			EntityIndex index = entity.index();

			// Fetch this before we delete the entity (only if info is enabled)
			#ifdef SEECS_INFO_ENABLED
				std::string entityInfo = GetEntityInfo(entity);
			#endif

			// Destroy component associations
			for (int i = 0; i < m_componentPools.size(); i++)
				if (mask[i] == 1 && m_componentPools[i])
					m_componentPools[i]->Delete(index);

			m_entityInfo[index].componentMask = {};
			m_entityInfo[index].version++;

			m_entityNames.Delete(index);
			m_availableEntities.push_back(index);

			entity.m_id = NULL_ENTITY_ID;

			SEECS_INFO("Deleted: " << entityInfo);
		}

		/*
		*  Register a component and create a pool for it
		*/
		template <typename T>
		void RegisterComponent() {
			size_t ind = GetComponentIndex<T>();
			SEECS_ASSERT(ind < MAX_COMPONENTS,
				"Exceeded max number of registered components");

			if (ind >= m_componentPools.size())
				m_componentPools.resize(ind + 1);

			SEECS_ASSERT(!m_componentPools[ind],
				"Attempting to register component '" << typeid(T).name() << "' twice");

			m_componentPools[ind] = std::make_unique<SparseSet<T>>();

			SEECS_INFO("Registered component '" << typeid(T).name() << "'");
		}

		/*
		*  Attaches a component to an entity
		*
		* - Add<Transform>(player, {x, y, z});
		*/
		template <typename T>
		T& Add(Entity entity, T&& component = {}) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = GetComponentPool<T>();

			EntityIndex index = entity.index();

			// If component already exists, overwrite
			if (pool.Get(index))
				return *pool.Set(index, std::move(component));

			ComponentMask& mask = GetEntityComponentMask(entity);

			SetComponentBit<T>(mask, 1);

			SEECS_INFO("Attached '" << typeid(T).name() << "' to " << GetEntityInfo(entity));
			return *pool.Set(index, std::move(component));
		}

		/*
		*  Retrieves the specified component for the given entity
		*
		* - ecs.Get<Transform>(player);
		*/
		template <typename T>
		T& Get(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = GetComponentPool<T>();
			EntityIndex index = entity.index();
			T* component = pool.Get(index);
			SEECS_ASSERT(component,
				GetEntityInfo(entity) << " missing component in '" << typeid(T).name() << "' pool");

			return *component;
		}

		/*
		*  Retrieves a pointer to the specified component for the given entity
		*
		* - ecs.GetPtr<Transform>(player);
		*/
		template <typename T>
		T* GetPtr(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = GetComponentPool<T>();
			return pool.Get(entity.index());
		}

		/*
		*  Removes a component from an entity
		*
		* - ecs.Remove<Transform>(player);
		*/
		template <typename T>
		void Remove(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			SparseSet<T>& pool = GetComponentPool<T>();

			EntityIndex index = entity.index();

			if (!pool.Get(index)) return;

			ComponentMask& mask = GetEntityComponentMask(entity);
			SetComponentBit<T>(mask, 0);

			pool.Delete(index);
			SEECS_INFO("Removed '" << typeid(T).name() << "' from " << GetEntityInfo(entity));
		}

		template <typename... Ts>
		bool Has(Entity entity) {
			if (!IsAlive(entity))
				return false;

			auto& mask = GetEntityComponentMask(entity);
			return (GetComponentBit<Ts>(mask) && ...);
		}

		template <typename... Ts>
		bool HasAny(Entity entity) {
			return (Has<Ts>(entity) || ...);
		}

		/*
		*   Create a SimpleView instance which you can iterate via .ForEach()
		* 
		*   - auto view = ecs.View<A, B>();
		*/
		template <typename... Components>
		SimpleView<Components...> View() {
			return { this };
		}

		/*
		*  Queries whether the entity is currently 'alive' and can be operated on by the ECS
		*/
		bool IsAlive(Entity entity) const {
			if (entity.id() == NULL_ENTITY_ID)
				return false;

			if (entity.index() >= m_entityInfo.size())
				return false;

			// "Alive" simply means checking if the version of the given entity ID is equal to 
			// the latest version in entity info; which is incremented upon deletion
			return m_entityInfo[entity.index()].version == entity.version();
		}

		size_t GetPoolCount() const {
			return m_componentPools.size();
		}

		void PrintEntityComponents(Entity entity) {
			SEECS_ASSERT_VALID_ENTITY(entity);
			SEECS_ASSERT_ALIVE_ENTITY(entity);

			std::stringstream ss;
			std::string prefix = "";
			ss << GetEntityInfo(entity) << " components: ";
			ComponentMask& mask = GetEntityComponentMask(entity);
			for (int i = 0; i < MAX_COMPONENTS; i++)
				if (mask[i] == 1) {
					ss << prefix << m_componentNames[i];
					prefix = ", ";
				}
			
			SEECS_MSG(ss.str());
		}

	};

	/*
	*  A SimpleView is a basic implementation of a view, allowing iteration based
	*  on the passed in Component parameter pack.
	*/
	template <typename... Components>
	class SimpleView {
	private:

		using componentTypes = type_list<Components...>;

		ECS* m_ecs;

		std::array<ISparseSet*, sizeof...(Components)> m_viewPools;
		std::vector<ISparseSet*> m_excludedPools;

		// Sparse set with the smallest number of components,
		// basis for ForEach iterations.
		ISparseSet* m_smallest = nullptr;

		/*
		*	Returns true iff all the pools in the view contain the given Entity
		*/
		bool AllContain(EntityIndex entityIndex) {
			return std::all_of(m_viewPools.begin(), m_viewPools.end(), [entityIndex](ISparseSet* pool) {
				return pool->ContainsIndex(entityIndex);
			});
		}

		bool NotExcluded(EntityIndex entityIndex) {
			if (m_excludedPools.empty()) return true;

			return std::none_of(m_excludedPools.begin(), m_excludedPools.end(), [entityIndex](ISparseSet* pool) {
				return pool->ContainsIndex(entityIndex);
			});
		}

		Entity MakeEntityFromIndex(EntityIndex entityIndex) {
			return Entity(
				m_ecs->m_entityInfo[entityIndex].version,
				entityIndex
			);
		}

		/*
		*	Index the generic pool array and downcast to a specific component pool
		*   by using compile time indices
		*/
		template <size_t Index>
		auto GetPoolAt() {
			using componentType = typename componentTypes::template get<Index>;
			return static_cast<SparseSet<componentType>*>(m_viewPools[Index]);
		}

		template <size_t... Indices>
		auto MakeComponentTuple(EntityIndex entityIndex, std::index_sequence<Indices...>) {
			return std::make_tuple((std::ref(GetPoolAt<Indices>()->GetRef(entityIndex)))...);
		}

		/*
		*  Provided the function arguments are valid, this function will iterate over the smallest pool
		*  and run the lambda on all entities that contain all the components in the view.
		*
		*  Note: This is the internal implementation: opt for the more user friendly functional ones in the
		*        public interface.
		*/
		template <typename Func>
		void ForEachImpl(Func&& func) {
			constexpr auto inds = std::make_index_sequence<sizeof...(Components)>{};

			// Iterate smallest component pool and compare against other pools in view
			// Note this list is a COPY, allowing safe deletion during iteration.
			for (EntityIndex index : m_smallest->GetIndexList()) {
				if (AllContain(index) && NotExcluded(index)) {
					
					// This branch is for [](EntityID id, Component& c1, Component& c2);
					// constexpr denotes this is evaluated at compile time, which prunes
					// invalid function call branches before runtime to prevent the
					// typical invoke errors you'd see after building.
					if constexpr (std::is_invocable_v<Func, Entity, Components&...>) {
						Entity entity = MakeEntityFromIndex(index);
						std::apply(func, std::tuple_cat(std::make_tuple(entity), MakeComponentTuple(index, inds)));
					}

					// This branch is for [](Component& c1, Component& c2);
					else if constexpr (std::is_invocable_v<Func, Components&...>) {
						std::apply(func, MakeComponentTuple(index, inds));
					}

					else {
						SEECS_ASSERT(false,
							"Bad lambda provided to .ForEach(), parameter pack does not match lambda args");
					}
				}
			}
		}

	public:

		// These are the function signatures you can pass to .ForEach()
		using ForEachFunc = std::function<void(Components&...)>;
		using ForEachFuncWithID = std::function<void(Entity, Components&...)>;

		SimpleView(ECS* ecs) :
			m_ecs(ecs), m_viewPools{ ecs->GetComponentPoolPtr<Components>()... }
		{
			SEECS_ASSERT(componentTypes::size == m_viewPools.size(), "Component type list and pool array size mismatch");

			auto smallestPool = std::min_element(m_viewPools.begin(), m_viewPools.end(),
				[](ISparseSet* a, ISparseSet* b) { return a->Size() < b->Size(); }
			);

			SEECS_ASSERT(smallestPool != m_viewPools.end(), "Initializing invalid/empty view");

			m_smallest = *smallestPool;
		}

		template <typename... ExcludedComponents>
		SimpleView& Without() {
			m_excludedPools = { m_ecs->GetComponentPoolPtr<ExcludedComponents>()... };
			return *this;
		}

		/*
		*  Executes a passed lambda on all the entities that match the
		*  passed parameter pack.
		*
		*  Provided function should follow one of two forms:
		*  [](Component& c1, Component& c2);
		*  OR
		*  [](EntityID id, Component& c1, Component& c2);
		*/
		void ForEach(ForEachFunc func) {
			ForEachImpl(func);
		}

		void ForEach(ForEachFuncWithID func) {
			ForEachImpl(func);
		}

		/*
		*	Holds an entity id and a tuple of references to the components returned by the view.
		*	Access components that are part of a pack like such:
		*	- auto [componentA, componentB] = pack.components;
		*/
		struct Pack {
			Entity entity;
			std::tuple<Components&...> components;
		};

		/*
		*  Useful when you want a way to iterate a view via indices.
		*  e.g:
			auto packed = ecs.View<A, B>().GetPacked();
			for (size_t i = 0; i < packed.size(); i++) {
				auto [a1, b1] = packed[i].components;
			}
		*/
		std::vector<Pack> GetPacked() {
			constexpr auto inds = std::make_index_sequence<sizeof...(Components)>{};
			std::vector<Pack> result;

			for (EntityIndex index : m_smallest->GetIndexList())
				if (AllContain(index) && NotExcluded(index)) {
					result.push_back({ MakeEntityFromIndex(index), MakeComponentTuple(index, inds)});
				}
			return result;
		}


	};

}

#endif