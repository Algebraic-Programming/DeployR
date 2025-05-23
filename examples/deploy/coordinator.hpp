#include <deployr/deployr.hpp>

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