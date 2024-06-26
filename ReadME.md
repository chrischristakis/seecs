# seecs

seecs (pronounced see-ks) is a small header only ECS implementation for C++. Seecs stands for **Simple-Enough-Entity-Component-System**, which defines the primary goal:

To implement only the core of a functional ECS as a resource for learning. 

It's my take on a 'pure' ECS using sparse sets to optimize data locality, in which entities are just IDs, components are data, and (most importantly) systems query entities based on components and operate that on data.

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

Systems are omitted from this project. This is because seecs provides everything you need to get a system running, and I don't want to force you into some rigid structure just because I deem it best.

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

Deleting an entity during runtime is a bit of a struggle with an ECS, since you don't want to directly delete an entity with `.DeleteEntity(id)` while iterating with `.View()` or `.ForEach()`, since they both iterate the list of active entities internally.

So I've baked a system into the ECS to help with this, primarily with two functions,
- `.FlagEntity(id, flag)`: Entity with `id` will be flagged for deletion if `flag` is `true` and unmarked if `flag` is `false`
- `.DeleteFlagged()` Should be called at the end of a frame and will delete all entities that are flagged

So for example:
```cpp
ecs.ForEach<HealthComponent>([&ecs](EntityID id, HealthComponent& hc) {
  if (hc.health <= 0)
    ecs.FlagEntity(id, true);   
});

// Other systems...

ecs.DeleteFlagged();
```

This allows graceful deletion of entities, as well as the ability to potentially resurrect entities from another system by using `.FlagEntity(id, false)` before the frame ends.

By default, `.View()`, `.ForEach()` and `.ViewIDs()` will not return flagged entities, but if you'd like a system that considers entities that were flagged this frame before deletion, you can set the optional `includeFlagged` parameter in these
functions to `true`, i.e:
```cpp
ecs.ForEach<HealthComponent>([&ecs](EntityID id, HealthComponent& hc) {
  // ...
}, true); <---- This system will iterate flagged entities too
```

### Things I'll get around to:

- Events
- Copying
- Serialization (...?)
- Documentation

This project just one part of a project I'm working on, and I decided to release it on its own. This means improvements to seecs will roll around when they are needed in the main project.
