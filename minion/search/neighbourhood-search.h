
#ifndef MINION_NEIGHBOURHOOD_SEARCH_H
#define MINION_NEIGHBOURHOOD_SEARCH_H
#include "NeighbourhoodChoosingStrategies.h"
#include "neighbourhood-def.h"
#include <cstdlib>
#include <ctime>
#include <memory>

template <typename NeighbourhoodSelectionStrategy>
struct NeighbourhoodSearchManager : public Controller::SearchManager {
  shared_ptr<Propagate> prop;
  vector<SearchOrder> base_order;
  NeighbourhoodContainer nhc;
  shared_ptr<NeighbourhoodSelectionStrategy> selectionStrategy;
  NeighbourhoodSearchManager(shared_ptr<Propagate> _prop, vector<SearchOrder> _base_order,
                             NeighbourhoodContainer _nhc,
                             shared_ptr<NeighbourhoodSelectionStrategy> selectionStrategy)
      : prop(std::move(_prop)),
        base_order(_base_order),
        nhc(_nhc),
        selectionStrategy(std::move(selectionStrategy)) {}

  void searchNeighbourhoods(vector<DomainInt>& solution, DomainInt& minValue,
                            vector<int>& activatedNeighbourhoods) {
    // Save state of the world
    int depth = Controller::get_world_depth();
    Controller::world_push();

    auto vo = Controller::make_search_order_multiple(base_order);
    if(activatedNeighbourhoods.empty()) {
      nhc.shadow_disable.assign(1);
    } else {
      switchOnNeighbourhoods(activatedNeighbourhoods, solution);
    }
    solution.clear();

    prop->prop(vo->getVars());
    if(getState().isFailed()) {
      if(activatedNeighbourhoods.empty()) {
        D_FATAL_ERROR("Problem unsatisfiable with all neighbourhoods turned off");
      } else {
        Controller::world_pop_to_depth(depth);
        return;
      }
    }

    auto sm = make_shared<Controller::StandardSearchManager>(
        vo, prop, Controller::standard_time_ctrlc_checks,
        [&solution, &minValue, this]() {
          for(const auto& var : this->nhc.shadow_mapping[0])
            solution.push_back(var.getAssignedValue());
          minValue = getState().getOptimiseVar()->getMin();
          throw EndOfSearch();
        },
        []() {});
    try {
      sm->search();
    } catch(EndOfSearch&) {}
    Controller::world_pop_to_depth(depth);
  }

  virtual void search() {
    vector<DomainInt> solution;
    DomainInt minValue;
    vector<int> activatedNeighbourhoods;

    searchNeighbourhoods(solution, minValue, activatedNeighbourhoods);

    if(solution.empty()) {
      return;
    }
    while(
        !(activatedNeighbourhoods = selectionStrategy->getNeighbourHoodsToActivate(nhc)).empty() &&
        getState().getOptimiseVar()->getDomSize() > 0) {
      if(!solution.empty()) {
        cout << "Found solution with op min " << minValue << "\nTrying minValue " << (minValue + 1)
             << endl;
        ;
        copyOverIncumbent(solution);
        getState().getOptimiseVar()->setMin(minValue + 1);
        std::vector<AnyVarRef> emptyVars;
        prop->prop(emptyVars);
      }

      auto startTime = std::chrono::high_resolution_clock::now();
      searchNeighbourhoods(solution, minValue, activatedNeighbourhoods);
      auto endTime = std::chrono::high_resolution_clock::now();
      u_int64_t timeTaken = getTimeTaken(startTime, endTime);
      selectionStrategy->updateStats(activatedNeighbourhoods,
                                    NeighbourhoodStats(minValue, timeTaken, !solution.empty()));
    }
  }

  typedef std::chrono::high_resolution_clock::time_point timePoint;

  u_int64_t getTimeTaken(timePoint startTime, timePoint endTime) {
    return std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
  }


  /**
   * Switch on the neighbourhood activation vars
   * Find the set of primary variables not contained in any neighbourhoods and assign them to the
   * incumbent solution
   * @param neighbourHoodIndexes
   */
  void switchOnNeighbourhoods(const vector<int>& neighbourHoodIndexes, const vector<DomainInt> &solution) {
    std::unordered_set<AnyVarRef> shadowVariables;
    for(int i : neighbourHoodIndexes) {
      cout << "Activating neighbourhood " << i << endl;
      Neighbourhood& neighbourhood = nhc.neighbourhoods[i];
      neighbourhood.activation.assign(1);

      for(auto& n : neighbourhood.vars) {
        shadowVariables.insert(n);
      }
    }

    /*
     * If the primary variable is not contained in any shadow neighbourhoods, assign its incumbent
     * solution
     */
    for(int i = 0; i < solution.size(); i++){
      if(shadowVariables.count(nhc.shadow_mapping[0][i]) == 0) {
        nhc.shadow_mapping[0][i].assign(solution[i]);
      }
    }
  }

  void copyOverIncumbent(const vector<DomainInt>& solution) {
    if (Controller::get_world_depth() != 1) {
      Controller::world_pop();
    }

    Controller::world_push();
    for(int i = 0; i < nhc.shadow_mapping[0].size(); i++) {
      nhc.shadow_mapping[1][i].assign(solution[i]);
    }

  }
};

shared_ptr<Controller::SearchManager> MakeNeighbourhoodSearch(PropagationLevel prop_method,
                                                              vector<SearchOrder> base_order,
                                                              NeighbourhoodContainer nhc) {
  shared_ptr<Propagate> prop = Controller::make_propagator(prop_method);
  return std::make_shared<NeighbourhoodSearchManager<RandomNeighbourhoodChooser>>(
      prop, base_order, nhc, std::make_shared<RandomNeighbourhoodChooser>(nhc));
}

#endif
