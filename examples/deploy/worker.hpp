#include <deployr/deployr.hpp>

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