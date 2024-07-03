# seecs

seecs (pronounced see-ks) is a small header only ECS sparse set implementation for C++. Seecs stands for **Simple-Enough-Entity-Component-System**, which defines the primary goal:

To implement the core of a functional ECS using sparse sets as a resource for learning, while still keeping it efficient.

It's my take on a 'pure' ECS, in which entities are just IDs, components are data, and (most importantly) systems query entities based on components and operate that on data.

Here's an example of seecs in action:
```cpp
#define SEECS_INFO_ENABLED  // Enables info messages during runtime
#include "seecs.h"

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
  
  ecs.RegisterComponent<A>();
  ecs.RegisterComponent<B>();
  ecs.RegisterComponent<C>();
  
  seecs::EntityID e1 = ecs.CreateEntity();
  seecs::EntityID e2 = ecs.CreateEntity("e2"); // Can name entities, will reflect in debug messages
  seecs::EntityID e3 = ecs.CreateEntity();
  
  ecs.Add<A>(e1, {5});  // initialize component A with x = 5
  ecs.Add<B>(e1); // default constructor called
  ecs.Add<C>(e1);
  
  ecs.Add<A>(e2);
  
  ecs.Add<A>(e3);
  ecs.Add<C>(e3);

  // This will run on all entities with components A and C
  ecs.ForEach<A, C>([&ecs](seecs::EntityID id, A& a, C& c) {
    // ...
  });
  
  // OR
  
  ecs.ForEach<A, C>([](A& a, C& c) {
    // ...
  });
  
  // OR
  
  for (auto& [id, a, c] : ecs.View<A, C>()) {
    // ...
  }
}
```

## Systems

Systems are not enforced in seecs. This is because it provides you with everything you need to get a system running, and I don't want to force you into some rigid structure just because I deem it best.

If you want to know how I add systems in seecs, I simply just do something like this:
```cpp
namespace MovementSystem {

  void Move(ECS& ecs) {
    ecs.ForEach<Transform, Physics>([](Transform& transform, Physics& physics) {
      transform.position += physics.velocity;
    });
  }

}
```

And that's it. It's on you to manage these systems however you want. You can make them function like I did here, or make a system it's own class that might even manage the entities belonging to it, whatever.

## Deleting entities

Deleting an entity during runtime can be tricky, since you don't want to directly delete an entity with `.DeleteEntity(id)` while iterating with `.View()` or `.ForEach()`, since they both iterate the list of active entities internally.

A good way to delete entities is to mark them down and deal with them gracefully after iterating them via `.View<...>()` or `.ForEach<...>(...)`

So for example:
```cpp
vector<EntityID> marked;
	ecs.ForEach<HealthComponent>([&ecs, &marked](EntityID id, HealthComponent& hc) {
		if(hc.health <= 0)
		  marked.push_back(id);
});

// Safely delete after iteration!
for (EntityID id : marked)
  ecs.DeleteEntity(id);
```

### Things I'll get around to:

- Events
- Copying
- Serialization (...?)
- Documentation

This project just one part of a project I'm working on, and I decided to release it on its own. This means improvements to seecs will roll around when they are needed in the main project.

A big thanks to [EnTT](https://github.com/skypjack/entt) and Austin Morian's [ECS article](https://austinmorlan.com/posts/entity_component_system/), which were both invaluable when learning about the concepts used for this project.