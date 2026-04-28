#define SEECS_INFO_ENABLED // Enable verbose logging
#include "seecs.h"
#include "benchmark.h"

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