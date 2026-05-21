#include "WpHashChain.hpp"

extern "C" __EXPORT int wp_hash_chain_main(int argc, char *argv[]);

int wp_hash_chain_main(int argc, char *argv[])
{
	return ModuleBase::main(WpHashChain::desc, argc, argv);
}
