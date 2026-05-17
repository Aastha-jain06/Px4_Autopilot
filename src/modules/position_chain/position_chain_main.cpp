#include "PositionChain.hpp"

extern "C" __EXPORT int position_chain_main(int argc, char *argv[]);

int position_chain_main(int argc, char *argv[])
{
	return ModuleBase::main(PositionChain::desc, argc, argv);
}
