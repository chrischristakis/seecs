# seecs

seecs (pronounced see-ks) is a small header only RTTI ECS sparse set implementation for C++. Seecs stands for **Simple-Enough-Entity-Component-System**, which defines the primary goal:

To implement the core of a functional ECS using sparse sets as a resource for learning, while still keeping it efficient.

It's my take on a 'pure' ECS, in which entities are just IDs, components are data, and (most importantly) systems query entities based on components and operate that on data.

Here's an example of seecs in action:
```cpp
#define SEECS_INFO_ENABLED
#include "seecs.h"

// Components hold data
struct A {
	int x = 0;
};

struct B {
	int y = 0;
};

struct C {
	int z = 0;
};

int main() {

	// Base ECS instance, acts as a coordinator
	seecs::ECS ecs;

	seecs::EntityID e1 = ecs.CreateEntity();
	seecs::EntityID e2 = ecs.CreateEntity("e2"); // Custom name for debugging
	seecs::EntityID e3 = ecs.CreateEntity();
	seecs::EntityID e4 = ecs.CreateEntity();
	seecs::EntityID e5 = ecs.CreateEntity();

	ecs.Add<A>(e1, {5});  // Initialize component A(5)
	ecs.Add<B>(e1); // Default constructor called
	ecs.Add<C>(e1);

	ecs.Add<A>(e2);

	ecs.Add<A>(e3);
	ecs.Add<C>(e3);

	ecs.Add<B>(e4);

	ecs.Add<A>(e5);
	ecs.Add<C>(e5);

	auto view = ecs.View<A, B>();
	view.ForEach([&](seecs::EntityID id, A& a, B& b) {
		// ...
	});

	auto packed = view.GetPacked();
	for (auto [id, components]: packed) {
		auto [a, b] = components;
	}

}
```

### Benchmarks

Specs: AMD Ryzen 5 5600x (6 cores, 3.7 GHz), Compiled via Visual Studio 2022 on a windows machine.


| Entities                 | 100      | 10,000 | 1,000,000 |
| --------                 | ---      | ------ | --------- |
| `CreateEntity`           | 0.0010ms | 0.34ms | 18.7ms    |
| `Add<T>`                 | 0.0024ms | 0.56ms | 56.3ms    |
| `Get<T>`                 | 0.0004ms | 0.22ms | 22.5ms    |
| `Remove<T>`              | 0.0006ms | 0.45ms | 45.7ms    |
| `DeleteEntity`           | 0.0011ms | 0.79ms | 95.9ms    |
| `ForEach (2 components)` | 0.0001ms | 0.10ms | 12.9ms    |
| `ForEach (4 components)` | 0.0002ms | 0.17ms | 21.3ms    |

- Note: These are IDEAL CONDITIONS in which the sparse set is densley populated and packed. Mileage may vary on use case.

## Systems

Systems are not enforced in seecs. This is because it provides you with everything you need to get a system running, and I don't want to force you into some rigid structure just because I deem it best.

If you want to know how I add systems in seecs, I simply just do something like this:
```cpp
namespace MovementSystem {

  void Move(ECS& ecs) {
    ecs.View<Transform, Physics>().ForEach([](Transform& transform, Physics& physics) {
      transform.position += physics.velocity;
    });
  }

}
```

And that's it. It's on you to manage these systems however you want. You can make them function like I did here, or make a system it's own class that might even manage the entities belonging to it, whatever.

## Deleting entities

seecs makes deleting entities easy and can de done directly while iterating:

```cpp
view.ForEach([&ecs](EntityID id, HealthComponent& hc) {
    ecs.DeleteEntity(id);
});
```

You can also safely add/remove components while iterating without encountering undefined behaviour:
```cpp
view.ForEach([&ecs](EntityID id, HealthComponent& hc) {
    ecs.Remove<HealthComponent>(id);
    ecs.Add<NewComponent>(id);
});

```

### How to access entities

You can access an entity in one of two ways currenty,

1) **Via views**

This is probably the most common way you'll access entities; by specifying a group of components and seecs will return all the entity IDs that match said group, like this:
```cpp
auto view = ecs.View<A, B>();

view.ForEach([](A& a, B& b) { //... });
```
Behind the scenes, a view takes the smallest of it's component pools and iterates all of the entities in it, checking if it has the other components.
This means when there's little overlap between entities that share components, there will be wasted iterations.
But in practise, I haven't run into this situation much; so I usually stick with views.


2) **Via ID lists**
   
If we know what components an entity will have beforehand, we can utilize the `Get` method and just extract all the components that we need:
```cpp
vector<EntityID> enemies;

void Update() {
   for (EntityID id : enemies) {
	Transform& transform = ecs.Get<Transform>(id);
	Health& health = ecs.Get<Health>(id);
   }
}
```
This is more rigid, and some call it an anti-pattern in an ECS, but it definitely has its merits and could potentially be more performant than views, since you won't waste any iterations.

There's merit (and obstacles) for each, but I'd recommend sticking to views until you see a need for ID lists. Or use both!

### Things I'll get around to:

- Events
- Copying
- Serialization (...?)

This project just one part of a project I'm working on, and I decided to release it on its own. This means improvements to seecs will roll around when they are needed in the main project.

A big thanks to [EnTT](https://github.com/skypjack/entt) and Austin Morian's [ECS article](https://austinmorlan.com/posts/entity_component_system/), which were both invaluable when learning about the concepts used for this project.
