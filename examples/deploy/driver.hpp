#include <deployr/deployr.hpp>
#include <fstream>

void coordinatorFc(deployr::DeployR &deployr)
{
  // Getting local instance
  const auto &instance = deployr.getLocalInstance();
  printf("[CoordinatorFc] Hi, I am '%s'\n", instance.getName().c_str());

  // Getting deployment information
  const auto &deployment = deployr.getDeployment();

  // Getting the originating request
  const auto &request = deployment.getRequest();

  // Getting the deployment channels
  const auto &channels = request.getChannels();

  // Sending messages to all the channels
  for (auto &c : channels)
  {
    // Creating message to send
    std::string message = std::string("Hello ") + c.getConsumer() + std::string("!");

    // Get channel name
    std::string channelName = c.getName();
    printf("[CoordinatorFc] Sending message '%s' through channel '%s'\n", message.c_str(), channelName.c_str());

    // Getting channel object
    auto &channel = deployr.getChannel(channelName);

    // Pushing message
    channel.push(message.data(), message.size());
  }
}

void workerFc(deployr::DeployR &deployr)
{
  // Getting local instance
  const auto &instance     = deployr.getLocalInstance();
  const auto &instanceName = instance.getName();
  printf("[WorkerFc] Hi, I am '%s'\n", instanceName.c_str());

  // Getting channel correspoding to this worker
  const std::string channelName = std::string("Coordinator -> ") + instanceName;
  auto             &channel     = deployr.getChannel(channelName);

  // Getting a message token from the coordinator
  auto token = channel.peek();
  while (token.success == false) token = channel.peek();

  std::string message((const char *)token.buffer, token.size);
  printf("[WorkerFc] %s received message from coordinator: '%s'\n", instanceName.c_str(), message.c_str());
}

void driver(deployr::DeployR &deployr, const char* deployrConfigFilePath)
{
  bool isRoot = deployr.getCurrentInstance().isRootInstance();
  
  // Registering Functions
  deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
  deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // Initializing deployr
  deployr.initialize();

  // If I'm root, do the deployment
  if (isRoot)
  {
    // Parsing DeployR configuration file contents to a JSON object
    std::ifstream ifs(deployrConfigFilePath);
    auto          deployrConfigJs = nlohmann::json::parse(ifs);

    // Creating request
    deployr::Request request(deployrConfigJs);

    // Deploying request, getting deployment
    deployr.deploy(request);
  }
}