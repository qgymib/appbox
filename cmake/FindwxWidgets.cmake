# Prevent duplicate entries
if(TARGET wx::core)
    set(wxWidgets_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_WXWIDGETS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/wxWidgets")
get_filename_component(_WXWIDGETS_SOURCE_DIR "${_WXWIDGETS_SOURCE_DIR}" ABSOLUTE)

# Configuration for wxWidgets
set(wxBUILD_SHARED OFF)
set(wxBUILD_TESTS OFF)
set(wxBUILD_SAMPLES OFF)
set(wxBUILD_DEMOS OFF)
set(wxBUILD_PRECOMP OFF)
set(wxBUILD_INSTALL OFF)

set(wxUSE_DOC_VIEW_ARCHITECTURE OFF)
set(wxUSE_HELP OFF)
set(wxUSE_MEDIACTRL OFF)
set(wxUSE_MDI OFF)
set(wxUSE_MDI_ARCHITECTURE OFF)
set(wxUSE_MS_HTML_HELP OFF)
set(wxUSE_WXHTML_HELP OFF)
set(wxUSE_WEBVIEW OFF)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_WXWIDGETS_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/wxWidgets-build")

set(wxWidgets_FOUND TRUE)
