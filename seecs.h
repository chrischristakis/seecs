#ifndef SEECS_ECS_H
#define SEECS_ECS_H

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

// Can replace these defines with custom macros elsewhere
#ifndef SEECS_ASSERTS
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

namespace seecs {

	// In ECS, entities are simply just indices which group data
	using EntityID = uint64_t;

	static constexpr EntityID NULL_ENTITY = std::numeric_limits<EntityID>::max();

	// Max amoutn of entities alive at once.
	// Set this to NULL_ENTITY if you want no limit.
	// Once limit is hit, an assert will fire and
	// the program will terminate.
	constexpr size_t MAX_ENTITIES = 1000000;

	// Should be a multiple of 32 (4 bytes) - 1, since
	// bitset overallocates by 4 bytes each time.
	constexpr size_t MAX_COMPONENTS = 64;

	// Reserve this number of components for dense to avoid initial resize/copy.
	constexpr size_t DENSE_INITIAL_SIZE = 100;

	// Base class allows runtime polymorphism
	class IComponentPool {
	public:
		virtual ~IComponentPool() { };
		virtual void Delete(EntityID) = 0;
	};

	/*
	* A component pool is a container of one type of component
	* * i.e. ComponentPool<Transform>
	* 
	* This container is unaware of what a valid entity might be, so it's
	* important that it is managed by the ECS as a coordinator.
	*/ 
	template <typename T>
	class ComponentPool: public IComponentPool {
	private:

		using Page = size_t;
		using Sparse = std::vector<size_t>;

		// Tombstone used to act as a null value
		static constexpr size_t tombstone = std::numeric_limits<size_t>::max();

		// Used for sparse pagination
		static constexpr size_t SPARSE_MAX_SIZE = 5000;

		// Dense is tightly packed and contains components.
		// Its size is how many active components there are.
		std::vector<T> m_dense;

		// Maps index in dense vector to the corresponding entity ID
		std::vector<EntityID> m_denseToEntityID;

		// Sparse is paginated since it's possible for an entity 
		// with a massive ID to be the only entity with this component, 
		// so to avoid allocating a sparse vector with a size == massiveID 
		// where all the previous elements go unused, split it into chunks/pages
		std::vector<Sparse> m_sparseSets;

		/*
		* Inserts a given dense index into the sparse vector, associating
		* an entity ID with the index in the dense vector.
		* 
		* This doesnt actually insert anything into the dense
		* vector, it simply defines a mapping from ID -> index
		*/
		void InsertDenseIndex(EntityID id, size_t index) {
			Page page = id / SPARSE_MAX_SIZE;
			size_t sparseIndex = id % SPARSE_MAX_SIZE;

			if (page >= m_sparseSets.size())
				m_sparseSets.resize(page + 1);
			
			Sparse& sparse = m_sparseSets[page];
			if (sparseIndex >= sparse.size())
				sparse.resize(sparseIndex + 1);

			sparse[sparseIndex] = index;
		}

		/* 
		* Returns the dense index for a given entity ID,
        * or a tombstone (null) value if non-existent
		*/
		size_t GetDenseIndex(EntityID id) {
			Page page = id / SPARSE_MAX_SIZE;
			size_t sparseIndex = id % SPARSE_MAX_SIZE;

			if (page < m_sparseSets.size()) {
				Sparse& sparse = m_sparseSets[page];
				if (sparseIndex < sparse.size())
					return sparse[sparseIndex];
			}

			return tombstone;
		}

	public:

		ComponentPool() {
			m_dense.reserve(DENSE_INITIAL_SIZE);
		}

		/*
		* Attempt to remove a component associated with an entity.
		* 
		* Can be used on a base instance
		*/
		void Delete(EntityID id) override {
			if(m_dense.empty())
				return;

			size_t deletedIndex = GetDenseIndex(id);

			if (deletedIndex == tombstone)
				return;

			size_t backIndex = m_dense.size() - 1;
			EntityID backEntityID = m_denseToEntityID[backIndex];

			// Switch back of dense with element to delete
			std::swap(m_dense[backIndex], m_dense[deletedIndex]);
			std::swap(m_denseToEntityID[backIndex], m_denseToEntityID[deletedIndex]);

			// Update sparse info
			InsertDenseIndex(backEntityID, deletedIndex);
			InsertDenseIndex(id, tombstone);

			// Deleted is now at back, pop it off
			m_dense.pop_back();
			m_denseToEntityID.pop_back();
		}

		/*
		* Appends the given T type into the component pool, and adds
		* the index into the sparse list while appending to the dense
		* list as well.
		* 
		* This function WILL copy the component param due to not 
		* using perfect forwarding. This is intentional as I prefer
		* not having to define constructors and I like the whole
		* AddComponent(id, {properties}) notation.
		*/
		T& Add(EntityID id, T&& component) {
			SEECS_ASSERT(GetDenseIndex(id) == tombstone, 
				"'" << typeid(T).name() << "' pool sparse ID: '" << id << "' already occupied when adding");

			InsertDenseIndex(id, m_dense.size());
			m_dense.push_back(component);
			m_denseToEntityID.push_back(id);

			return m_dense.back();
		}

		T& Get(EntityID id) {
			size_t denseIndex = GetDenseIndex(id);
			SEECS_ASSERT(denseIndex != tombstone, 
				"'" << typeid(T).name() << "' pool attempting to Get() sparse ID: " << id << " with no dense index'")
			return m_dense[denseIndex];
		}

	};


	class ECS {
	private:

		// Each bit in the mask represents a component,
		// '1' means it is active '0' means inactive.
		// Mask[0] declares if entity is alive or not
		using ComponentMask = std::bitset<1 + MAX_COMPONENTS>;

		using TypeName = const char*;

		// List of IDs already created, but no longer in use
		std::vector<EntityID> m_availableEntities;

		// List of entity ids alive and in use
		// pair[0] == id
		// pair[1] == flagged for deletion
		std::vector<std::pair<EntityID, bool>> m_alive;

		// index: Index into dense array 'm_alive', NULL_INDEX
		//        means the entity is not in m_alive, and is a
		//        fast way to check if an entity is active.
		// 
		// mask: Component mask belonging to an entity, used to
		//       quickly check if an entity has a component
		struct EntityInfo {
			size_t index = NULL_INDEX;
			ComponentMask mask;
		};

		// Index this array using entity ID for entity info
		// 
		// This isn't paginated because even if an index in this vector
		// isn't in use, an entity with that ID existed at one point
		// and will likely exist again thanks to m_availableEntities.
		std::vector<EntityInfo> m_entityInfo;

		// Associates ID with name provided in CreateEntity()
		std::unordered_map<EntityID, std::string> m_entityNames;

		// Holds generic pointers to specific component pools
		// Index into this array using the corresponding bit position
		// found in m_componentBitPosition
		std::vector<std::unique_ptr<IComponentPool>> m_componentPools;

		// Key is component name, value is the position in ComponentMask
		// corresponding to the component
		std::unordered_map<TypeName, size_t> m_componentBitPosition;

		// Highest recorded entity ID
		EntityID m_maxEntityID = 0;

		// Tombstone
		static constexpr size_t NULL_INDEX = std::numeric_limits<size_t>::max();

		#define ENTITY_INFO(id) \
			"['" << GetEntityName(id) << "', ID: " << id << "]"

		#define SEECS_ASSERT_VALID_ENTITY(id) \
			SEECS_ASSERT(id != NULL_ENTITY, "NULL_ENTITY cannot be operated on by the ECS") \
			SEECS_ASSERT(id < m_maxEntityID, "Invalid entity ID out of bounds: " << id) \
			SEECS_ASSERT(id < m_entityInfo.size(), "Attempting to access invalid m_entityInfo index, " << id);

		#define SEECS_ASSERT_ALIVE_ENTITY(id) \
			SEECS_ASSERT(m_entityInfo[id].index != NULL_INDEX, "Attempting to access dead entity with ID: " << id);
	
	private:

		template <typename T>
		size_t GetComponentBitPosition() {
			TypeName name = typeid(T).name();
			auto it = m_componentBitPosition.find(name);
			if (it == m_componentBitPosition.end())
				return NULL_INDEX;

			return it->second;
		}

		/*
		* Retrieves reference for the specific component pool given a component name
		*/
		template <typename T>
		ComponentPool<T>& GetComponentPool(bool registerIfNotFound = false) {
			size_t bitPos = GetComponentBitPosition<T>();

			if (bitPos == NULL_INDEX) {
				if (registerIfNotFound) {
					RegisterComponent<T>();
					bitPos = GetComponentBitPosition<T>();
				}
				SEECS_ASSERT(registerIfNotFound,
					"Attempting to operate on unregistered component '" << typeid(T).name() << "'");
			}

			SEECS_ASSERT(bitPos < m_componentPools.size() && bitPos >= 0,
				"(Internal): Attempting to index into m_componentPools with out of range bit position");

			// Downcast the generic pointer to the specific pool
			IComponentPool* genericPtr = m_componentPools[bitPos].get();
			ComponentPool<T>* pool = dynamic_cast<ComponentPool<T>*>(genericPtr);
			SEECS_ASSERT(pool, "(Internal): Dynamic cast failed for component pool '" << typeid(T).name() << "'");

			return *pool;
		}

		template <typename T>
		ComponentMask::reference GetEntityComponentBit(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);
			size_t bitPos = GetComponentBitPosition<T>();
			SEECS_ASSERT(bitPos != NULL_INDEX,
				"Attempting to operate on unregistered component '" << typeid(T).name() << "'");

			return m_entityInfo[id].mask[bitPos];
		}

	public:

		ECS() = default;

		/*
		*  Creates an entity and returns the ID to refer to that entity.
		* 
		*  @param(name): 
		*  * Optional and used for debugging purposes, it
		*    shouldn't be used often since there's no optimization 
		*    in place yet for entities that share a name.
		*/
		EntityID CreateEntity(std::string_view name="") {
			EntityID id = NULL_ENTITY;

			if (m_availableEntities.size() == 0) {
				SEECS_ASSERT(m_alive.size() <= MAX_ENTITIES, "Entity limit exceeded");

				id = m_maxEntityID++;
			}
			else {
				id = m_availableEntities.back();
				m_availableEntities.pop_back();
			}
			SEECS_ASSERT(id != NULL_ENTITY, 
				"Cannot create entity with NULL_ENTITY ID");
			
			// Make sure entity info can accomodate any new IDs
			if (id >= m_entityInfo.size())
				m_entityInfo.resize(id + 1);

			// Append to back of alive list and set appropriate index
			m_entityInfo[id].index = m_alive.size();
			m_entityInfo[id].mask.reset();
			m_alive.push_back({ id, false });

			if (!name.empty())
				m_entityNames[id] = name;

			SEECS_INFO("Created entity " << ENTITY_INFO(id));
			return id;
		}

		std::string GetEntityName(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);
			auto it = m_entityNames.find(id);
			if (it == m_entityNames.end())
				return "Entity";
			return it->second;
		}

		/*
		* Deletes an alive entity and resets its associated components.
		* Also sets the given entity ID to NULL_ENTITY
		* 
		* This should NOT be used in the middle of a system while iterating
		* through entities, as it will remove from the list immediately. Use
		* FlagEntity(id, true) to mark an entity for deletion, and then DeleteFlagged()
		* At the end of a frame to clear all flagged entities.
		*/
		void DeleteEntity(EntityID& id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);
			SEECS_ASSERT(id < m_entityInfo.size() && m_entityInfo[id].index != NULL_INDEX,
				"Cannot delete inactive entity with ID: " << id);

			// Remove from m_alive by swapping with back element,
			// then popping back off, and switch indices of the
			// swapped element and deleted one in entity info vector.
			size_t backIndex = m_alive.size() - 1;
			size_t deletedIndex = m_entityInfo[id].index;
			EntityID backID = m_alive[backIndex].first;

			std::swap(m_alive[backIndex], m_alive[deletedIndex]);
			m_alive.pop_back();

			m_entityInfo[backID].index = deletedIndex;
			
			// Other housekeeping
			std::string name = GetEntityName(id);
			m_entityInfo[id].index = NULL_INDEX;
			m_entityInfo[id].mask.reset();
			m_availableEntities.push_back(id);
			m_entityNames.erase(id);

			// Destroy component associations
			for (auto& pool : m_componentPools)
				pool->Delete(id);

			SEECS_INFO("Deleted entity ['" << name << "', ID: " << id << "]");
			id = NULL_ENTITY;
		}

		/*
		* Register a component and create a pool for it
		* - Should be done on initialization of the game instance,
		*   but do whatever you want.
		*/
		template <typename T>
		void RegisterComponent() {
			TypeName name = typeid(T).name();
			SEECS_ASSERT(m_componentBitPosition.find(name) == m_componentBitPosition.end(),
				"Component with name '" << name << "' already registered");
			SEECS_ASSERT(m_componentPools.size() < MAX_COMPONENTS,
				"Exceeded max number of registered components");

			m_componentBitPosition.insert({name, m_componentPools.size()});
			m_componentPools.push_back(std::make_unique<ComponentPool<T>>());

			SEECS_INFO("Registered component '" << name << "'");
		}

		/*
		* Adds a component to the associated entity and into the component pool
		* 
		* - AddComponent<Transform>(player, {x, y, z});
		*/
		template <typename T>
		T& Add(EntityID id, T&& component={}) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			// Do this first so component pool is registered
			ComponentPool<T>& pool = GetComponentPool<T>(true);

			ComponentMask::reference componentBit = GetEntityComponentBit<T>(id);
			SEECS_ASSERT(componentBit == 0,
				ENTITY_INFO(id) << " already has component '" << typeid(T).name() << "' added");

			componentBit = 1;

			SEECS_INFO("Attached '" << typeid(T).name() << "' to " << ENTITY_INFO(id));
			return pool.Add(id, std::move(component));
		}

		/*
		* Retrieves the specified component for the given entity ID
		* 
		* - ecs.GetComponent<Transform>(player);
		*/
		template <typename T>
		T& Get(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			SEECS_ASSERT(Has<T>(id),
				ENTITY_INFO(id) << " has no component '" << typeid(T).name() << "' to get");

			ComponentPool<T>& pool = GetComponentPool<T>(true);  // register component if not found
			return pool.Get(id);
		}

		/*
		* Removes a component from the entity
		* 
		* - ecs.RemoveComponent<Transform>(player);
		*/
		template <typename T>
		void Remove(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			ComponentMask::reference componentBit = GetEntityComponentBit<T>(id);
			SEECS_ASSERT(componentBit == 1,
				ENTITY_INFO(id) << " has no component '" << typeid(T).name() << "' to remove");

			componentBit = 0;

			ComponentPool<T>& pool = GetComponentPool<T>();
			pool.Delete(id);
			SEECS_INFO("Removed '" << typeid(T).name() << "' from " << ENTITY_INFO(id));
		}

		/*
		* Flags an entity to be deleted in the alive list
		* calling DeleteFlagged() will remove all of the
		* entities and set them to inactive when called.
		*/
		void FlagEntity(EntityID id, bool flagDelete) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			size_t i = m_entityInfo[id].index;
			m_alive[i].second = flagDelete;
		}

		/*
		* Deletes all entities in m_alive that are flagged using
		* FlagEntity(...). 
		* Use this at the end of a frame after all systems are ran in
		* order to gracefully delete flagged entities.
		*/
		void DeleteFlagged() {
			std::vector<EntityID> marked;
			for (auto& [id, flag] : m_alive)
				if (flag)
					marked.push_back(id);

			for (EntityID id : marked)
				DeleteEntity(id);
		}

		template <typename... Ts>
		bool HasAll(EntityID id) {
			// Fold operator, reads as 
			// (HasComponent<Transform> && HasComponent<Physics> && HasComponent<Sprite> && ...)
			return (Has<Ts>(id) && ...);
		}

		template <typename T>
		bool Has(EntityID id) {
			return GetEntityComponentBit<T>(id) == 1;
		}

		/*
		* Gets all the entity IDs matching the component parameter pack,
		* if ViewIDS(true), it will include entities flagged for deletion
		* in the list of returned IDs
		* 
		* for (EntityID id : ecs.View<Transform, Sprite>()) {
		*   ...
		* }
		*/
		template <typename ...Components>
		std::vector<EntityID> ViewIDs(bool includeFlagged = false) {
			std::vector<EntityID> matched;

			for (auto& [id, flagged] : m_alive)
				if ((includeFlagged || !flagged) && HasAll<Components...>(id)) 
					matched.push_back(id);

			return matched; // NRVO should move this
		}

		/*
		* Returns tuple containing the id of entity and all
		* valid components matching parameter pack
		* if View(true), it will include entities flagged for deletion
		* in the list of returned IDs.
		*
		* for (auto& [id, a, b] : ecs.ViewEach<A, B>()) {
		*   ...
		* }
		*/
		template <typename ...Components>
		std::vector<std::tuple<EntityID, Components&...>> View(bool includeFlagged=false) {
			std::vector<std::tuple<EntityID, Components&...>> matched;

			for (auto& [id, flagged] : m_alive)
				if ((includeFlagged || !flagged) && HasAll<Components...>(id))
					matched.emplace_back(id, Get<Components>(id)...);

			return matched;
		}
		 
		/*
		* Executes a passed lambda on all the entities that match the
		* parameter pack criteria.
		* If ForEach([]() {}, true), it will include entities flagged for deletion
		* in the list of returned IDs
		* 
		* Provided function should follow one of two forms:
		* [](Component& c1, Component& c2);
		* OR 
		* [](EntityID id, Component& c1, Component& c2);
		*/
		template <typename ...Components, typename Func>
		void ForEach(Func&& func, bool includeFlagged=false) {
			for (EntityID id : ViewIDs<Components...>(includeFlagged)) {
				
				// This branch is for [](EntityID id, Component& c1, Component& c2);
				// constexpr denotes this is evaluated at compile time, which allows
				// the calling of func with different parameters.
				if constexpr (std::is_invocable_v<Func, EntityID, Components&...>) {
					func(id, Get<Components>(id)...);
				}

				// This branch is for [](Component& c1, Component& c2);
				else if constexpr (std::is_invocable_v<Func, Components&...>) {
					func(Get<Components>(id)...);
				}

				else {
					SEECS_ASSERT(false,
						"Bad lambda provided to .ForEach(), parameter pack does not match lambda args");
				}
			}
		}

		void PrintAlive() {
			std::stringstream ss;
			std::string delim = "";
			for (auto& [id, flagged] : m_alive) {
				ss << delim << ENTITY_INFO(id);
				if (flagged)
					ss << " Marked for deletion";
				if(delim.empty()) delim = "\n";
			}
			SEECS_INFO("Alive: \n" << ss.str());
		}

		void PrintEntityComponents(EntityID id) {
			SEECS_ASSERT_VALID_ENTITY(id);
			SEECS_ASSERT_ALIVE_ENTITY(id);

			ComponentMask& mask = m_entityInfo[id].mask;

			std::stringstream ss;
			std::string delim = "";
			for (auto& [name, pos] : m_componentBitPosition) {
				if (mask[pos])
					ss << delim << " - " << name;
				if (delim.empty()) delim = "\n";
			}
			SEECS_INFO(ENTITY_INFO(id) << " components:\n" << ss.str());
		}

	};

}

#endif