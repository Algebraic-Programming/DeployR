#include <deployr/deployr.hpp>

int main(int argc, char* argv[])
{
    printf("Initializing DeployR\n");
    deployr::DeployR _deployr;
    _deployr.initialize(&argc, &argv);
    printf("Done.\n");
}