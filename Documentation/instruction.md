# General Installation

## Webinterface
### Step 1
When installing Totalsync please make sure the name of the folder is Totalsync without any addition.
The format should be ``\your_path\TotalSync ``
### Step 2
It's recommended to install in a conda environment. For that install and open a anaconda prompt and type:
```
conda create --name totalsync
conda activate totalsync
conda install pip
```
### Step 4
GO to your TotalSync folder with this command
```
cd \your_path\TotalSync
```
### Step 5
Once in the folder you can use the following command to install TotalSync
````
pip install -e .
````
### Step 6
Following this installation you can start TotalSync with the command `totalsync` in the command prompt

### After installation

You can run now TotalSync at any time by opening the command prompt and type:
```
conda activate totalsync
totalsync
```

## Arduino

Open the arduino code and send it in the teensy, you can modify it as wanted. Few examples are already available in this github
