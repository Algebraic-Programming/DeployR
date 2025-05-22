#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>
#include "coordinator.hpp"
#include "worker.hpp"

int main(int argc, char* argv[])
{
    // Creating DeployR instance
    deployr::DeployR deployr;

    // Creating Functions
    deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
    deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

    // Initializing DeployR. Only one instance (root) continues from here
    deployr.initialize(&argc, &argv);

    // Checking arguments
    if (argc != 2)
    {
        fprintf(stderr, "Error: Must provide the request file as argument.\n");
        deployr.abort();
        return -1;
    }

    // Getting request file name from arguments
    std::string requestFilePath = std::string(argv[1]);

    // Parsing request file contents to a JSON object
    std::ifstream ifs(requestFilePath);
    auto requestJs = nlohmann::json::parse(ifs);

    // Creating request
    deployr::Request request(requestJs);

    // Deploying request, getting deployment
    deployr.deploy(request);

    // Finalize
    deployr.finalize();

    printf("Finished Successfully\n");
}