import os
import site
for p in site.getsitepackages():
    cm_dir = os.path.join(p, 'idf_component_manager')
    if os.path.isdir(cm_dir):
        print("Found CM in", cm_dir)
        import subprocess
        subprocess.run(f"grep -rn 'platformdirs' {cm_dir}", shell=True)
