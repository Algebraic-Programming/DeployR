#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>

int main(int argc, char* argv[])
{
    // Creating DeployR instance
    deployr::DeployR _deployr;

    // Creating Functions
    _deployr.registerFunction("CoordinatorFc", []() { printf("Hi, I am coordinator\n"); });
    _deployr.registerFunction("WorkerFc", []() { printf("Hi, I am worker\n"); });

    // Initializing DeployR. Only one instance (root) continues from here
    _deployr.initialize(&argc, &argv);

    // Checking arguments
    if (argc != 2)
    {
        fprintf(stderr, "Error: Must provide the request file as argument.\n");
        _deployr.abort();
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
    _deployr.deploy(request);

    // Finalize
    _deployr.finalize();

    printf("Finished Successfully\n");
}