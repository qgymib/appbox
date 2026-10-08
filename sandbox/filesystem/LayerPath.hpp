#ifndef APPBOX_SANDBOX_FILESYSTEM_LAYER_PATH_HPP
#define APPBOX_SANDBOX_FILESYSTEM_LAYER_PATH_HPP

#include <string>

namespace appbox::filesystem
{

/**
 * @brief Translate the path of a layer back into the path of the view.
 *
 * The helper is the counterpart of the mapping the resolver applies to a view
 * path: the upper layer rebases a view path below its own root by encoding the
 * drive of the view path as the first component of the layer path, and a lower
 * layer rebases the view path of its `mapped_nt_path` below its
 * `host_nt_path`. A path which is below one of those roots is answered with
 * the path of the view it belongs to, so a caller which holds the path of a
 * layer can report the path the sandboxed process knows.
 *
 * Both paths are the form the file system reports for the name of a handle,
 * which starts at the root of the volume of the handle: a file below
 * `\??\C:\Windows` is named `\Windows\...`. The drive is not part of that form,
 * so it is not part of the result either; the upper layer still carries it as
 * the first component of the layer path, which is what makes the translation
 * possible at all.
 *
 * The comparison is case insensitive and matches whole components, so a layer
 * root which is a prefix of the queried path without being a component
 * boundary does not match. The upper layer is matched first, then the lower
 * layers in the order they are mounted.
 *
 * @param[in] layerPath Path of the upper layer, of a lower layer or of the
 *                      host layer, relative to the root of its volume.
 * @param[out] viewPath Path of the view the layer path belongs to, in the same
 *                      form.
 * @return true when the path is below a layer of the view, false when it is
 *         not and the caller has to keep the name as the file system reported
 *         it.
 */
bool RebaseLayerPathToView(const std::wstring& layerPath, std::wstring& viewPath);

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_LAYER_PATH_HPP
