#pragma once

namespace iprop {

// Entry point of the ipgen CLI: generates a .iprop file to a PNG and prints
// the result JSON. Returns the process exit code (0 = success).
int ipgenMain(int argc, char* argv[]);

} // namespace iprop
