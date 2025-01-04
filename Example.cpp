#define SEECS_INFO_ENABLED
#include "seecs.h"
#include "benchmark.h"

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

	// OR

	view.ForEach([&](A& a, B& b) {
		// ...
	});

	// OR

	auto packed = view.GetPacked();
	for (auto [id, components] : packed) {
		auto [a, b] = components;
		// ...
	}
}