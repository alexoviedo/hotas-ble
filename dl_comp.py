import urllib.request, json, os, tarfile
url = "https://api.components.espressif.com/components/espressif/usb_host_ext_hub"
req = urllib.request.Request(url)
with urllib.request.urlopen(req) as response:
    data = json.loads(response.read().decode())
    download_url = data['versions'][0]['url']
    print("Downloading from", download_url)
    urllib.request.urlretrieve(download_url, "ext_hub.tgz")
    os.makedirs("components/usb_host_ext_hub", exist_ok=True)
    with tarfile.open("ext_hub.tgz", "r:gz") as tar:
        tar.extractall("components/usb_host_ext_hub")
print("Done")
