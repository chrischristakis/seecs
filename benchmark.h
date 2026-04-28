#pragma once
#include <chrono>
#include <vector>
#include <numeric>
#include "seecs.h"

class Timer {
private:

	using Clock = std::chrono::steady_clock;
	using Duration = Clock::duration;
	using TimePoint = Clock::time_point;

	TimePoint m_start;

	struct MillisecondsTraits {
		using unit = std::milli;
		static constexpr std::string_view UNIT_STRING = "ms";
	};

	struct MicrosecondsTraits {
		using unit = std::micro;
		static constexpr std::string_view UNIT_STRING = "Us";
	};

	struct SecondsTraits {
		using unit = std::ratio<1>;
		static constexpr std::string_view UNIT_STRING = "s";
	};

	using SelectedTrait = MicrosecondsTraits;

public:

	struct TimerTraits {

		using unit = SelectedTrait::unit;
		static constexpr std::string_view UNIT_STRING = SelectedTrait::UNIT_STRING;
	};

	Timer() {
		reset();
	}

	// Return time in seconds since timer was last reset
	float elapsed() {
		return std::chrono::duration<float, TimerTraits::unit>(Clock::now() - m_start).count();
	}

	void reset() {
		m_start = Clock::now();
	}

};

template <typename T>
struct Dummy {
	T data;
};

inline void run_benchmark(const size_t I, const size_t iterations) {
	using namespace seecs;

	SEECS_MSG("Running benchmark for " << I << " entities with " << iterations << " iterations...")

	std::vector<float> creation_results;
	std::vector<float> add_component_results;
	std::vector<float> get_component_results;
	std::vector<float> remove_component_results;
	std::vector<float> delete_entity_results;
	std::vector<float> for_each_two_components_results;
	std::vector<float> get_two_components_results;
	std::vector<float> for_each_four_components_results;
	std::vector<float> get_four_components_results;

	for (int iteration = 0; iteration < iterations; iteration++) {
		Timer t;
		ECS ecs;

		std::vector<Entity> entities;
		entities.resize(I);

		// CREATION
		t.reset();
		for (size_t i = 0; i < I; i++)
			entities[i] = ecs.create_entity();
		float creation_result = t.elapsed();
		creation_results.push_back(creation_result);

		// ADD COMPONENT
		t.reset();
		for (size_t i = 0; i < I; i++) {
			ecs.add<Dummy<int>>(entities[i], {});
		}
		float add_component_result = t.elapsed();
		add_component_results.push_back(add_component_result);

		// GET COMPONENT
		t.reset();
		for (size_t i = 0; i < I; i++) {
			ecs.get<Dummy<int>>(entities[i]);
		}
		float get_component_result = t.elapsed();
		get_component_results.push_back(get_component_result);


		// REMOVE COMPONENT
		t.reset();
		for (size_t i = 0; i < I; i++) {
			ecs.remove<Dummy<int>>(entities[i]);
		}
		float remove_component_result = t.elapsed();
		remove_component_results.push_back(remove_component_result);

		// DELETE ENTITY
		t.reset();
		for (size_t i = 0; i < I; i++) {
			ecs.delete_entity(entities[i]);
		}
		float delete_entity_result = t.elapsed();
		delete_entity_results.push_back(delete_entity_result);

		ecs.reset();
		for (size_t i = 0; i < I; i++) {
			entities[i] = ecs.create_entity();
			ecs.add<Dummy<int>>(entities[i], {});
			ecs.add<Dummy<double>>(entities[i], {});
		}

		// FOR EACH (2 COMPONENTS)
		auto view1 = ecs.view<Dummy<int>, Dummy<double>>();
		t.reset();
		view1.for_each([](Dummy<int>& _, Dummy<double>& __) {});
		float for_each_two_components_result = t.elapsed();
		for_each_two_components_results.push_back(for_each_two_components_result);

		// GET (2 COMPONENTS)
		t.reset();
		for (size_t i = 0; i < I; i++) {
			ecs.get<Dummy<int>>(entities[i]);
			ecs.get<Dummy<double>>(entities[i]);
		}
		float get_two_components_result = t.elapsed();
		get_two_components_results.push_back(get_two_components_result);

		ecs.reset();
		for (size_t i = 0; i < I; i++) {
			entities[i] = ecs.create_entity();
			ecs.add<Dummy<int>>(entities[i], {});
			ecs.add<Dummy<double>>(entities[i], {});
			ecs.add<Dummy<long>>(entities[i], {});
			ecs.add<Dummy<float>>(entities[i], {});
		}

		// FOREACH (4 COMPONENTS)
		auto view2 = ecs.view<Dummy<int>, Dummy<double>, Dummy<long>, Dummy<float>>();
		t.reset();
		view2.for_each([](Dummy<int>& _, Dummy<double>& __, Dummy<long>& ___, Dummy<float>& ____) {});
		float for_each_four_components_result = t.elapsed();
		for_each_four_components_results.push_back(for_each_four_components_result);

		// GET (4 COMPONENTS)
		t.reset();
		for (size_t i = 0; i < I; i++) {
			ecs.get<Dummy<int>>(entities[i]);
			ecs.get<Dummy<double>>(entities[i]);
			ecs.get<Dummy<long>>(entities[i]);
			ecs.get<Dummy<float>>(entities[i]);
		}
		float get_four_components_result = t.elapsed();
		get_four_components_results.push_back(get_four_components_result);

		SEECS_MSG("Finished iteration " << iteration << "...")
	}

	auto calculate_average = [&](const std::vector<float>& input, std::string_view test_name) {
		float average = std::accumulate(input.begin(), input.end(), 0.0f) / input.size();
		SEECS_MSG("Average execution for: '" << test_name << "': " << average << Timer::TimerTraits::UNIT_STRING);
	};

	calculate_average(creation_results, "creation");
	calculate_average(add_component_results, "add component");
	calculate_average(get_component_results, "get component");
	calculate_average(remove_component_results, "remove component");
	calculate_average(delete_entity_results, "delete entity");
	calculate_average(for_each_two_components_results, "for_each (2 components)");
	calculate_average(get_two_components_results, "get component (2 components)");
	calculate_average(for_each_four_components_results, "for_each (4 components)");
	calculate_average(get_four_components_results, "get component (4 components)");
}