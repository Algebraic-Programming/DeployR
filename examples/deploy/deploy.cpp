#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>

int main(int argc, char* argv[])
{
    // Creating DeployR instance
    deployr::DeployR _deployr;
    
    // Initializing DeployR
    printf("Initializing DeployR\n");
    _deployr.initialize(&argc, &argv);

    // If I am the root instance, read the deployment file. Otherwise, keep it empty
    nlohmann::json deploymentJs;
    if (_deployr.isRootInstance() == true)
    {
        // Checking arguments
        if (argc != 2)
        {
            fprintf(stderr, "Error: Must provide the deployment file as argument.\n");
            _deployr.abort();
            return -1;
        }

        // Getting filename
        std::string deploymentFilePath = std::string(argv[1]);

        // Parsing file contents to a JSON object
        std::ifstream ifs(deploymentFilePath);
        deploymentJs = nlohmann::json::parse(ifs);
    }

    // Creating deployment
    deployr::Deployment deployment(deploymentJs);

    // Deploying
    _deployr.deploy(deployment);

    // Finalize
    _deployr.finalize();

    printf("Finished Successfully\n");
}