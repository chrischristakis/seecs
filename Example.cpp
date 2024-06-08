#define SEECS_INFO_ENABLED
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
	seecs::EntityID e2 = ecs.CreateEntity("e2");
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