//#define SEECS_INFO_ENABLED
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

	/*
	* 
	* TODO: 
	* 1) Naming convention update (snake_case for functions+properties, PascalCase for types, CAPS_SNAKE for consts)
	* 2) Run benchmarks and log into readme in microseconds
	* 3) update readme with new conventions
	* 4) Close/Reply to issues on github
	* 
	*/

	RunBenchmark(1000000);
	//	
	//// Base ECS instance, acts as a coordinator
	//seecs::ECS ecs;

	//seecs::Entity e1 = ecs.CreateEntity();
	//seecs::Entity e2 = ecs.CreateEntity("e2"); // Custom name for debugging
	//seecs::Entity e3 = ecs.CreateEntity();
	//seecs::Entity e4 = ecs.CreateEntity();
	//seecs::Entity e5 = ecs.CreateEntity();

	//ecs.Add<A>(e1, { 5 });  // Initialize component A(5)
	//ecs.Add<B>(e1); // Default constructor called
	//ecs.Add<C>(e1);

	//ecs.Add<A>(e2);

	//ecs.Add<A>(e3);
	//ecs.Add<C>(e3);

	//ecs.Add<A>(e4, { 100 });
	//ecs.Add<B>(e4, { 200 });

	//ecs.Add<A>(e5);
	//ecs.Add<C>(e5);

	//auto view = ecs.View<A, B>();

	//view.ForEach([&](seecs::Entity entity, A& a, B& b) {
	//	// ...
	//});

	//// OR

	//// Components with component 'A' but not 'B' OR 'C'
	//auto excludedView = ecs.View<A>().Without<B, C>();
	//excludedView.ForEach([&](A& a) {
	//	// ...
	//});

	//// OR

	//auto packed = view.GetPacked();
	//for (auto [entity, components] : packed) {
	//	auto [a, b] = components;
	//	// ...
	//}

}