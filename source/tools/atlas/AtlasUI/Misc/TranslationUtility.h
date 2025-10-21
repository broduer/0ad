#include <wx/string.h>
#include <wx/translation.h>

template <typename T>
inline wxString tr(T&& str)
{
	if constexpr (std::is_array_v<std::remove_reference_t<T>>)
		return wxUnderscoreWrapper(str);
	else
		return wxGetTranslation(str);
}
