# The custom target adds no sources of its own: it only turns on a capability
# the stock KL25Z entry leaves out.
add_library(mbed-pinscape-kl25z INTERFACE)
target_link_libraries(mbed-pinscape-kl25z INTERFACE mbed-kl25z)
