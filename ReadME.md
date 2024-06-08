# seecs

seecs (pronounced see-ks) is a small header only ECS implementation for C++. Seecs stands for **Simple-Enough-Entity-Component-System**, which defines the primary goal:

To implement only the core of a functional ECS as a resource for learning. 

This is not meant to be the fastest, or the best ECS. It's my take on a 'pure' ECS in which entities are identifies, components are data, and (most importantly) systems query entities based on components and operate that on data.

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
  
  ecs.Add<A>(e1, {5});  // initialize component A(5)
  ecs.Add<B>(e1); // default constructor called
  ecs.Add<C>(e1);
  
  ecs.Add<A>(e2);
  
  ecs.Add<A>(e3);
  ecs.Add<C>(e3);
  
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

### Things I'll get around to:

- Events
- Copying
- Serialization (...?)
- Documentation

This project just one part of a project I'm working on, and I decided to release it on its own. This means improvements to seecs will roll around when they are needed in the main project.
