# Fix broken model

OrcaSlicer could assist you in fixing broken models. Once broken model is detected, error message is shown with suggestion to fix the model. Models repair is done using external tools and available methods are depending on the platform OrcaSlicer is running on.

Please note that model repair assistance is not perfect and may not work for your specific case. In this case you may need to use external tools.

## Windows

If OrcaSlicer is running on Windows 10 or newer it will use Microsoft APIs for repairing models through Netfabb.

## Linux and MacOS

There are no standard mesh repairing software provided on Linux and MacOS systems. However it is possible to use [PyMeshLab](https://pymeshlab.readthedocs.io) for the task. 

1. Install PyMeshLab according to official documentation.
    \
    Simple installation may be done in next steps:
    ```    
    # create virtualenv directory
    python3 -m venv pymeshlab
    # activate virtualenv
    source pymeshlab/bin/activate
    # install pymeshlab
    pip3 install pymeshlab
    ``` 
2. Configure OrcaSlicer to use specific installation of PyMeshLab.
   \
   Open Preferences and navigate down to the 'Paths' sections. Click 'Browse' button on the right of 'PyMeshLab' line and select your installation: 
   \
   For example you have pymeshlab installed in your virtual env at `~/venv/pymeshlab`. In this case navigate and select folder `~/venv/pymeshlab/bin/` - the one containing venv activate scripts.
