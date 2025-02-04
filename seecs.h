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

	// In ECS, entities are simply just indices which group data
	using EntityID = uint64_t;


	static constexpr EntityID NULL_ENTITY = std::numeric_limits<EntityID>::max();


	// Max amount of entities alive at once.
	// Set this to NULL_ENTITY if you want no limit.
	// Once limit is hit, an assert will fire and
	// the program will terminate.
	constexpr size_t MAX_ENTITIES = NULL_ENTITY;


	// Should be a multiple of 32 (4 bytes), since
	// bitset overallocates by 4 bytes each time.
	constexpr size_t MAX_COMPONENTS = 64;


	// Base class allows runtime polymorphism
	class ISparseSet {
	public:
		virtual ~ISparseSet() = default;
		virtual void Delete(EntityID) = 0;
		virtual void Clear() = 0;
		virtual size_t Size() = 0;
		virtual bool ContainsEntity(EntityID id) = 0;
		virtual std::vector<EntityID> GetEntityList() = 0;
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

		static constexpr size_t SPARSE_MAX_SIZE = 2048;
		static constexpr size_t tombstone = std::numeric_limits<size_t>::max();

		using Sparse = std::array<size_t, SPARSE_MAX_SIZE>;

		std::vector<Sparse> m_sparsePages;

		std::vector<T> m_dense;
		std::vector<EntityID> m_denseToEntity; // 1:1 vector where dense index == Entity Index

		/*
		* Inserts a given dense index into the sparse vector, associating
		* an Entity ID with the index in the dense vector.
		*
		* This doesnt actually insert anything into the dense
		* vector, it simply defines a mapping from ID -> index
		*/
		inline void SetDenseIndex(EntityID id, size_t index) {
			size_t page = id / SPARSE_MAX_SIZE;
			size_t sparseIndex = id % SPARSE_MAX_SIZE; // Index local to a page

			if (page >= m_sparsePages.size()) {
				m_sparsePages.resize(page + 1);
				m_sparsePages[page].fill(tombstone);
			}

			Sparse& sparse = m_sparsePages[page];

			sparse[sparseIndex] = index;
		}

		/*
		* Returns the dense index for a given entity ID,
		* or a tombstone (null) value if non-existent
		*/
		inline size_t GetDenseIndex(EntityID id) {
			size_t page = id / SPARSE_MAX_SIZE;
			size_t sparseIndex = id % SPARSE_MAX_SIZE;

			if (page < m_sparsePages.size()) {
				Sparse& sparse = m_sparsePages[page];
				return sparse[sparseIndex];
			}

			return tombstone;
		}

	public:

		SparseSet() {
			// Avoids initial copies/allocation, feel free to alter size
			m_dense.reserve(1000);
			m_denseToEntity.reserve(1000);
		}

		T* Set(EntityID id, T obj) {
			// Overwrite existing elements
			size_t index = GetDenseIndex(id);
			if (index != tombstone) {
				m_dense[index] = obj;
				m_denseToEntity[index] = id;

				return &m_dense[index];
			}

			// New index will be the back of the dense list
			SetDenseIndex(id, m_dense.size());

			m_dense.push_back(obj);
			m_denseToEntity.push_back(id);

			return &m_dense.back();
		}

		T* Get(EntityID id) {
			size_t index = GetDenseIndex(id);
			return (index != tombstone) ? &m_dense[index] : nullptr;
		}

		T& GetRef(EntityID id) {
			size_t index = GetDenseIndex(id);
			if (index == tombstone)
				SEECS_ASSERT(false, "GetRef called on invalid entity with ID " << id);
			return m_dense[index];
		}

		void Delete(EntityID id) override {

			size_t deletedIndex = GetDenseIndex(id);

			if (m_dense.empty() || deletedIndex == tombstone) return;

			SetDenseIndex(m_denseToEntity.back(), deletedIndex);
			SetDenseIndex(id, tombstone);

			std::swap(m_dense.back(), m_dense[deletedIndex]);
			std::swap(m_denseToEntity.back(), m_denseToEntity[deletedIndex]);

			m_dense.pop_back();
			m_denseToEntity.pop_back();
		}

		size_t Size() override {
			return m_dense.size();
		}

		std::vector<EntityID> GetEntityList() override {
			return m_denseToEntity;
		}

		bool ContainsEntity(EntityID id) override {
			return GetDenseIndex(id) != tombstone;
		}

		void Clear() override {
			m_dense.clear();
			m_sparsePages.clear();
			m_denseToEntity.clear();
		}

		bool IsEmpty() const {
			return m_dense.empty();
		}

		// Read-only dense list
		const std::vector<T>& Data() const {
			return m_dense;
		}

		void PrintDense() {
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



	/*
	*  A SimpleView is a basic implementation of a view, allowing iteration based
	*  on the passed in Component parameter pack.
	*/
	template <typename... Components>
	class SimpleView {
	private:

		using componentTypes = type_list<Components...>;

		std::array<ISparseSet*, sizeof...(Components)> m_viewPools;

		// Sparse set with the smallest number of components,
		// basis for ForEach iterations.
		ISparseSet* m_smallest = nullptr;

		/*
		*	Returns true iff all the pools in the view contain the given Entity
		*/
		bool AllContain(EntityID id) {
			return std::all_of(m_viewPools.begin(), m_viewPools.end(), [id](ISparseSet* pool) {
				return pool->ContainsEntity(id);
			});
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
		auto MakeComponentTuple(EntityID id, std::index_sequence<Indices...>) {
			return std::make_tuple((std::ref(GetPoolAt<Indices>()->GetRef(id)))...);
		}

		/*
		*  Provided the function arguments are valid, this function will iterate over the smallest pool
		*  and run the lambda on all entities that contain all the components in the view.
		* 
		*  Note: This is the internal implementation: opt for the more user friendly functional ones in the
		*        public interface.
		*/
		template <typename Func>
		void ForEachImpl(Func func) {
			constexpr auto inds = std::make_index_sequence<sizeof...(Components)>{};

			// Iterate smallest component pool and compare against other pools in view
			// Note this list is a COPY, allowing safe deletion during iteration.
			for (EntityID id : m_smallest->GetEntityList()) {
				if (AllContain(id)) {

					// This branch is for [](EntityID id, Component& c1, Component& c2);
					// constexpr denotes this is evaluated at compile time, which prunes
					// invalid function call branches before runtime to prevent the
					// typical invoke errors you'd see after building.
					if constexpr (std::is_invocable_v<Func, EntityID, Components&...>) {
						std::apply(func, std::tuple_cat(std::make_tuple(id), MakeComponentTuple(id, inds)));
					}

					// This branch is for [](Component& c1, Component& c2);
					else if constexpr (std::is_invocable_v<Func, Components&...>) {
						std::apply(func, MakeComponentTuple(id, inds));
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
		using ForEachFuncWithID = std::function<void(EntityID, Components&...)>;

		SimpleView(std::array<ISparseSet*, sizeof...(Components)> pools) :
			m_viewPools{ pools }
		{
			SEECS_ASSERT(componentTypes::size == m_viewPools.size(), "Component type list and pool array size mismatch");

			auto smallestPool = std::min_element(m_viewPools.begin(), m_viewPools.end(),
				[](ISparseSet* a, ISparseSet* b) { return a->Size() < b->Size(); }
			);

			SEECS_ASSERT(smallestPool != m_viewPools.end(), "Initializing invalid/empty view");

			m_smallest = *smallestPool;
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
			EntityID id;
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

			for (EntityID id : m_smallest->GetEntityList())
				if (AllContain(id))
					result.push_back({ id, MakeComponentTuple(id, inds) });
			return result;
		}


	};



	class ECS {
	private:

		// Each bit in the mask represents a component,
		// '1' == active, '0' == inactive.
		using ComponentMask = std::bitset<MAX_COMPONENTS>;


		// List of IDs already created, but no longer in use
		std::vector<EntityID> m_availableEntities;


		// Holds the component mask for an entity
		SparseSet<ComponentMask> m_entityMasks;


		// Associates ID with name provided in CreateEntity(), mainly for debugging
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
		EntityID m_maxEntityID = 0;


#define ENTITY_INFO(id) \
			"['" << GetEntityName(id) << "', ID: " << id << "]"

#define SEECS_ASSERT_VALID_ENTITY(id) \
			SEECS_ASSERT(id != NULL_ENTITY, "NULL_ENTITY cannot be operated on by the ECS") \
			SEECS_ASSERT(id < m_maxEntityID && id >= 0, "Invalid entity ID out of bounds: " << id);

#define SEECS_ASSERT_ALIVE_ENTITY(id) \
			SEECS_ASSERT(m_entityMasks.Get(id) != nullptr, "Attempting to access inactive entity with ID: " << id);

	private:

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
		ComponentMask::reference GetComponentBit(ComponentMask& mask) {
			size_t bitPos = GetComponentIndex<Component>();
			return mask[bitPos];
		}

		ComponentMask& GetEntityMask(EntityID id) {
			ComponentMask* mask = m_entityMasks.Get(id);
			SEECS_ASSERT(mask, "Entity " << ENTITY_INFO(id) << " has no component mask");
			return *mask;
		}

		/*
		*  Assembles a generic mask for the given components
		*/
		template <typename... Components>
		ComponentMask GetMask() {
			ComponentMask mask;
			(SetComponentBit<Components>(mask, 1), ...);
			return mask;
		}

	public:

		ECS() = default;

		template <typename T>
		static void Define() {
			static int index = 0;
			return index;
		}

		void Reset() {
			m_availableEntities.clear();
			m_entityMasks.Clear();
			m_entityNames.Clear();
			m_componentPools.clear();
			m_maxEntityID = 0;
		}

		/*
		*  Creates an entity and returns the ID to refer to that entity.
		*
		*  @param(name):
		*  * Optional and used for debugging purposes, it
		*    shouldn't be used often since there's no optimization
		*    in place yet for entities that share a name.
		*/
		EntityID CreateEntity(std::string name = "") {
			EntityID id = NULL_ENTITY;

			// Either spawn a new ID or recycle one
			if (m_availableEntities.size() == 0) {
				SEECS_ASSERT(m_maxEntityID < MAX_ENTITIES, "Entity limit exceeded");
				id = m_maxEntityID++;
			}
			else {
				id = m_availableEntities.back();
				m_availableEntities.pop_back();
			}

			SEECS_ASSERT(id != NULL_ENTITY, "Cannot create entity with null ID");

			m_entityMasks.Set(id, {});

			if (!name.empty())
				m_entityNames.Set(id, name);

			SEECS_INFO("Created entity " << ENTITY_INFO(id));
			return id;
		}

		std::string GetEntityName(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			auto name = m_entityNames.Get(id);
			if (name)
				return *name;

			return "Entity";
		}

		/*
		* Deletes an active entity and its associated components.
		* - Overwrites the given entity to NULL_ENTITY.
		*
		*/
		void DeleteEntity(EntityID& id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			std::string name = GetEntityName(id);
			ComponentMask& mask = GetEntityMask(id);

			// Destroy component associations
			for (int i = 0; i < MAX_COMPONENTS; i++)
				if (mask[i] == 1)
					m_componentPools[i]->Delete(id);

			m_entityMasks.Delete(id);
			m_entityNames.Delete(id);
			m_availableEntities.push_back(id);

			SEECS_INFO("Deleted entity ['" << name << "', ID: " << id << "]");
			id = NULL_ENTITY;
		}

		/*
		*  Register a component and create a pool for it
		*/
		template <typename T>
		void RegisterComponent() {
			SEECS_ASSERT(m_componentPools.size() <= MAX_COMPONENTS,
				"Exceeded max number of registered components");

			size_t ind = GetComponentIndex<T>();
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
		T& Add(EntityID id, T&& component = {}) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			SparseSet<T>& pool = GetComponentPool<T>();

			// If component already exists, overwrite
			if (pool.Get(id))
				return *pool.Set(id, std::move(component));

			ComponentMask& mask = GetEntityMask(id);

			SetComponentBit<T>(mask, 1);

			SEECS_INFO("Attached '" << typeid(T).name() << "' to " << ENTITY_INFO(id));
			return *pool.Set(id, std::move(component));
		}

		/*
		*  Retrieves the specified component for the given entity
		*
		* - ecs.Get<Transform>(player);
		*/
		template <typename T>
		T& Get(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			SparseSet<T>& pool = GetComponentPool<T>();
			T* component = pool.Get(id);
			SEECS_ASSERT(component,
				ENTITY_INFO(id) << " missing component in '" << typeid(T).name() << "' pool");

			return *component;
		}

		/*
		*  Retrieves a pointer to the specified component for the given entity
		*
		* - ecs.GetPtr<Transform>(player);
		*/
		template <typename T>
		T* GetPtr(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			SparseSet<T>& pool = GetComponentPool<T>();
			return pool.Get(id);
		}

		/*
		*  Removes a component from an entity
		*
		* - ecs.Remove<Transform>(player);
		*/
		template <typename T>
		void Remove(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			SparseSet<T>& pool = GetComponentPool<T>();

			if (!pool.Get(id)) return;

			ComponentMask& mask = GetEntityMask(id);
			SetComponentBit<T>(mask, 0);

			pool.Delete(id);
			SEECS_INFO("Removed '" << typeid(T).name() << "' from " << ENTITY_INFO(id));
		}

		template <typename... Ts>
		bool Has(EntityID id) {
			auto& mask = GetEntityMask(id);
			return (GetComponentBit<Ts>(mask) && ...);
		}

		template <typename... Ts>
		bool HasAny(EntityID id) {
			return (Has<Ts>(id) || ...);
		}

		/*
		*   Create a SimpleView instance which you can iterate via .ForEach()
		* 
		*   - auto view = ecs.View<A, B>();
		*/
		template <typename... Components>
		SimpleView<Components...> View() {
			// Pass a copy of array from fold expression into view.
			return { { GetComponentPoolPtr<Components>()... } };
		}

		size_t GetEntityCount() {
			return m_entityMasks.Size();
		}

		size_t GetPoolCount() {
			return m_componentPools.size();
		}

		void PrintEntityComponents(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			std::stringstream ss;
			std::string prefix = "";
			ss << ENTITY_INFO(id) << " components: ";
			ComponentMask& mask = GetEntityMask(id);
			for (int i = 0; i < MAX_COMPONENTS; i++)
				if (mask[i] == 1) {
					ss << prefix << m_componentNames[i];
					prefix = ", ";
				}
			
			SEECS_MSG(ss.str());
		}

	};

}

#endif