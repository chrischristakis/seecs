# seecs

seecs (pronounced see-ks) is a small header only RTTI ECS sparse set implementation for C++. Seecs stands for **Simple-Enough-Entity-Component-System**, which defines the primary goal:

To implement the core of a functional ECS using sparse sets as a resource for learning, while still keeping it efficient.

It's my take on a 'pure' ECS, in which entities are just IDs, components are data, and (most importantly) systems query entities based on components and operate that on data.

Here's an example of seecs in action:
```cpp
#define SEECS_INFO_ENABLED // Enable verbose logging
#include "seecs.h"

// Components simply hold data
struct A {
	int x = 0;
};

struct B {
	float y = 0;
};

struct C {
	bool z = false;
	int w = 0;
};

int main() {
	// Base ECS instance
	seecs::ECS ecs;
	
	// Create entities
	seecs::Entity entity1 = ecs.create_entity();
	seecs::Entity entity2 = ecs.create_entity("Entity 2"); // Custom name for debugging
	seecs::Entity entity3 = ecs.create_entity();
	seecs::Entity entity4 = ecs.create_entity();
	seecs::Entity entity5 = ecs.create_entity();
	
	// Attach components to entities
	ecs.add<A>(entity1, { 5 });  // Initialize component constructor A(5)
	ecs.add<B>(entity1); // Default constructor called
	C& c = ecs.add<C>(entity1); // Get a reference to the component
	
	ecs.add<A>(entity2);
	
	ecs.add<A>(entity3);
	ecs.add<C>(entity3);
	
	ecs.add<A>(entity4);
	ecs.add<B>(entity4);
	
	ecs.add<A>(entity5);
	ecs.add<C>(entity5);
	
	// Initialize a view to iterate entities with components A and B
	auto view = ecs.view<A, B>();
	view.for_each([&](seecs::Entity entity, A& a, B& b) {
		// Lambda executes on: entity1, entity4
	});
	
	// Initialize a view to iterate entities that have component A, but not B or C
	auto excluded_view = ecs.view<A>().without<B, C>();
	excluded_view.for_each([&](A& a) {
		// Lambda executes on: entity2
	});

	// Calculate view upfront into a 'packed' list and use a regular for loop
	auto packed = view.packed();
	for (auto [entity, components] : packed) {
		auto& [a, b] = components;
		// ...
	}
}
```

If you'd like a more practical example, I made a game with an older build of this library [here](https://github.com/chrischristakis/ClusterPutt)
Many of the functions are changed/deprecated, but the overall essence of component + system architecture is there.

Also as a disclaimer, I want to stress this is a LEARNING project for me. There's probably a lot here that can be optimized and I'm kinda putting myself out there by releasing potentially bad code but hey I would've loved if this repo existed when I started this project.

### Benchmarks

Specs: AMD Ryzen 5 5600x (6 cores, 3.7 GHz), Compiled via Visual Studio 2026 Release mode (x64) on a Windows machine.
Each test was ran for 1000 iterations (1,000,000 was ran for 50 iterations)

| Entities                  | 100    | 10,000 | 1,000,000 |
| --------                  | ---    | ------ | --------- |
| `create_entity`           | 2.72µs |  165µs |  19.3ms   |
| `add<T>`                  | 2.28µs |  189µs |  26.1ms   |
| `get<T>`                  | 0.63µs | 58.9µs |  4.91ms   |
| `remove<T>`               | 1.38µs |  129µs |  10.8ms   |
| `delete_entity`           | 1.97µs |  120µs |  13.6ms   |
| `for_each (2 components)` | 0.63µs | 60.1µs |   6.2ms   |
| `get<T> (2 components)`   | 1.17µs |  112µs |  10.3ms   |
| `for_each (4 components)` | 1.10µs |  108µs |  10.0ms   |
| `get<T> (4 components)`   | 2.29µs |  256µs |  20.3ms   |

- Note: These are IDEAL CONDITIONS in which the sparse set is densley populated and packed. Mileage may vary in practise.

## Systems

Systems are not enforced in seecs. This is because it provides you with everything you need to get a system running, and I don't want to force you into some rigid structure just because I deem it best.

If you want to know how I add systems in seecs, I simply just do something like this:
```cpp
void MovementSystem(seecs::ECS& ecs, float dt) {
	auto view = ecs.view<Transform, Physics>().without<Frozen>(); // Queries entities with Transform and Physics, but omits 'Frozen' entities

	view.for_each([dt](Entity entity, Transform& transform, Physics& physics) {
		transform.position += physics.velocity * dt;
	});
}
```

Then in my `tick` function in my scene:

```cpp
MovementSystem(ecs, deltaTime);
```

And that's it. It's on you to manage these systems however you want. You can make them function like I did here, or make a system it's own class that might even manage the entities belonging to it and track some frame-state, whatever.

Here are some functions you might find useful when making systems:
```
// delete entities and remove components
ecs.remove<A>(entity); // Removes A from entity
ecs.delete_entity(entity); // 'Deletes' entity, and removes its components.

// You can manually retrieve components via the Entity ID:
A& component = ecs.get<A>(entity);
A* component_ptr = ecs.get_ptr<A>(entity);

// You can also check if an entity has a component(s):
ecs.has<A, B, C>(entity); // true if entity has ALL components
ecs.has_any<A, B, C>(entity); // true if entity has ANY of the components

// You can also check the status of an entity if you choose to persist the handle:
ecs.is_alive(entity); // true if the entity has not been destroyed with delete_entity
```

### A quick guide to components

Components in an ECS should be nothing but data, and systems should handle the processing. Components typically should not reference or be aware of eachother.

Due to the way components are stored in sparse sets (where they can move around frequently during regular operation) a component must define
a default constructor, copy constructor, copy assignment operator, move constructor, and move assignment operator.

Most components should be trivially copyable/movable, so these should be defined implicitly by the compiler (and it's in your interest in a data-driven design to keep them that way!)

Here's an example of a nice, 'good' component:
```cpp
struct TransformComponent {
	Vec3 position;
	Vec3 scale;
	float rotation;
};
```

All of these fields can be copied/moved trivially, and sparse sets like that simplicity since interally they do copying/moving as entities get added/deleted.

Here's an example of a component you might run into trouble with:
```cpp
struct BadComponent {
	std::unordered_map<int, std::unique_ptr<int>> data;
}
```

This component will not compile, since seecs will try and move it around in memory, but `std::unique_ptr` is not copyable.

As a VERY general rule of thumb (which I have been on record for breaking) keep components as simple data. If you need to reference assets or other objects within a component, prefer to set up some handle system that interfaces with your engine, like so:

```cpp
using TextureHandle = std::uint32_t;

struct SpriteComponent {
	TextureHandle texture_handle; // Trivially copyable handle
	SpriteRegion sprite_region;
	bool flip_horizontal;
}

Texture* texture = texture_manager.get(sprite.texture_handle);
```

## Deleting entities

seecs makes deleting entities easy and can de done directly while iterating a view:

```cpp
view.for_each([&ecs](Entity entity, HealthComponent& hc) {
    ecs.delete_entity(entity); // safe!
});
```

You can also safely add/remove components while iterating without encountering undefined behaviour:
```cpp
view.for_each([&ecs](Entity entity, HealthComponent& hc) {
    ecs.remove<HealthComponent>(entity);
    ecs.add<NewComponent>(entity);
});
```

And you can check the status of an entity handle after iteration:
```cpp
Entity delete_me = ecs.create_entity();

ecs.is_alive(delete_me); // true

// pretend this is a view that picks up 'delete_me'
view.for_each([&ecs](Entity entity, ...) {
    ecs.delete_entity(entity);
});

ecs.is_alive(delete_me); // false
```

Though I advise against long lasting handles for entities that can be deleted; see `Entity versioning`

### How to access entities

You can access an entity in one of two ways currenty,

1) **Via views**

This is probably the most common way you'll access entities; by specifying a group of components and seecs will return all the entities that match said group, like this:
```cpp
auto view = ecs.view<A, B>();

view.for_each([](Entity entity, A& a, B& b) {
	//... 
});
```
Behind the scenes, a view takes the smallest of its component pools and iterates all of the entities in it, checking if every entity in the smallest pool has the other components.

```
SimpleView<A, B, C>
Pool(A) x x x x x x x
Pool(B) x x  <----- Pool B will be used to iterate the view (smallest pool)
Pool(C) x x x x x
```

And some pseudocode for what the view does when `for_each` is called:

```
for (EntityIndex in Pool(B)) {
	if (Pool(A).contains(EntityIndex) and Pool(C).contains(EntityIndex))
		executeLambda();
}
```

This means when there's little overlap between the component pools specified in a view, there will be wasted iterations. This seldom happens in practise,
and it's pretty contrived as you'll natrually have a lot of overlap when designing your entities/components.

2) **Via `packed()`**

This does something similar to views, but it returns a vector of tuples containing the entity and the components on-call.
This is useful when you want to iterate over the entities using traiditional for loops which is particularly helpful when nesting loops in a system:

```cpp
auto packed = view.packed(); // <--- computed list [entity, ...&component] structs!
for (auto [entity, components] : packed) {
	auto& [a, b] = components;
	// Do stuff ...
}
```

3) **Via Random access lookups**
   
If we know what components an entity will have beforehand, we can utilize the `get` method and just extract all the components that we need given an Entity:
```cpp
Entity player = ecs.create_entity();

// ...

Transform& transform = ecs.get<Transform>(player);
Health& health = ecs.get<Health>(player);
```
I'd advise against this when dealing with large volumes of entities as views are measured to be faster (check `Benchmarks`) but this certainly has it's uses
when dealing with singleton-esque long lived entities like the player or camera.

### Entity Versioning

The `Entity` type is just a trivial wrapper around some handle type, like this:

```cpp
struct Entity {
	std::uint64_t id;
}
```

But the `id` is an encoded value that is made from the following:
```
id = encode(EntityVersion, EntityIndex)
```

And you can query this info using:
```cpp
entity.index();
entity.version();
```

The upper `12` (subject to change) bits are for the version, while the lower `52` bits are used for the entity index.

The index is the primary 'identifier' for the entity, which we use as an index into the sparse sets. This means two `Entity` values with different `id`'s but the same `EntityIndex` will access the same sparse set data.

The version exists since when you delete entities, we recycle their `EntityIndex` for reuse the next time `create_entity` is called. The `EntityVersion` gives us a way to discern between two different `Entity` values that share an `EntityIndex`

For example:
```cpp
Entity e1 = ecs.create_entity(); // version: 0, index: 0

ecs.delete_entity(e1); // frees index 0, increments version for that index.

Entity e2 = ecs.create_entity(); // version: 1, index: 0

assert(e1 != e2); // true
```

Versions can wrap around though. The `MAX_VERSION_VALUE` is `4095` (`12` bits for version!) so it will eventually wrap around to `0` if you keep creating/deleting entities, meaning two different entities might share the same `id` and be treated as the same by the `ECS`
This isn't an issue in practise as it would require you to:
- Keep creating/deleting entities rapidly so they share the same index.
- Do that 4096 times.
- Keep the stale handle after all that happened and compare it to the new one.

All of this is to say, don't store `Entity` handles that persist for a long time if the entity it references can be deleted.


### Things I'll get around to:

This project just one part of a project I'm working on, and I decided to release it on its own. This means improvements to seecs will roll around when they are needed in the main project.

### Thanks!

A big (huge) thanks to [EnTT](https://github.com/skypjack/entt) and Austin Morian's [ECS article](https://austinmorlan.com/posts/entity_component_system/), which were both invaluable when learning about the concepts used for this project.

Also a thank you to everyone who to contributed and pointed out any issues:
- nassorc
- kiririn39 
- m6vrm
- wisnunugroho21
- Youstoubie
