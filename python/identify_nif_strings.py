def find_dds_strings(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    for i in range(len(data)):
        if data[i:i+4] == b".dds":
            start = i
            while start > 0 and 32 <= data[start-1] <= 126:
                start -= 1
            texture_path = data[start:i+4].decode('ascii', errors='ignore')
            print("Found texture path:", texture_path)

find_dds_strings("LS48.nif")