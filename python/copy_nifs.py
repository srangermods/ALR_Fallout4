def binary_patch_texture(nif_path, old_tex, new_tex):
    with open(nif_path, 'rb') as f:
        data = f.read()

    if old_tex.encode('utf-8') not in data:
        print(f"Texture '{old_tex}' not found in {nif_path}")
        return

    # Replace it (length must match!)
    new_data = data.replace(old_tex.encode('utf-8'), new_tex.encode('utf-8'))

    with open(nif_path, 'wb') as f:
        f.write(new_data)

    print(f"Patched {nif_path}: {old_tex} -> {new_tex}")


def copy_and_patch_nifs(src_file, index_start, numCopies):
    count = 1
    for i in range(index_start, (index_start+ numCopies)):
        index = index_start + count
        dst_file = f"LS{index}.nif"
        old_tex = f"Textures\\Interface\\ALR_Backgrounds\\{index_start}.dds"
        new_tex = f"Textures\\Interface\\ALR_Backgrounds\\{index}.dds"

        if len(old_tex) != len(new_tex):
            print(f"ERROR: '{new_tex}' must be exactly {len(old_tex)} characters long.")
            continue

        # Copy and patch
        with open(src_file, 'rb') as f:
            with open(dst_file, 'wb') as out:
                out.write(f.read())

        binary_patch_texture(dst_file, old_tex, new_tex)
        count = count + 1


copy_and_patch_nifs("LS100.nif", 100, 355)
