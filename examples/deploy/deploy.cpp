#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>

int main(int argc, char* argv[])
{
    printf("Initializing DeployR\n");
    nlohmann::json config;
    deployr::DeployR _deployr(config);
    _deployr.initialize(&argc, &argv);
    printf("Done.\n");
}