#pragma once

//=== ServiceContext ===
template<typename... Services>
struct ServiceContext {
	using Tuple = std::tuple<Services*...>;
};
