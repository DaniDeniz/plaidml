<img src="docs/images/plaid-final.png" height="200"></img>

*A platform for making deep learning work everywhere.*


[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://github.com/plaidml/plaidml/blob/master/LICENSE)  



- [Documentation](https://vertexai-plaidml.readthedocs-hosted.com/)
- [Installation Instructions](docs/install.rst)
- [Building PlaidML](docs/building.md)
- [Contributing](docs/contributing.rst)
- [Reporting Issues](#reporting-issues)


PlaidML is an advanced and portable tensor compiler for enabling deep learning 
on laptops, embedded devices, or other devices where the available 
computing hardware is not well supported or the available software stack contains 
unpalatable license restrictions.

PlaidML sits underneath common machine learning frameworks, enabling users to 
access any hardware supported by PlaidML. PlaidML supports Keras, ONNX, and nGraph.

As a component within the [nGraph Compiler stack], PlaidML further extends the 
capabilities of specialized deep-learning hardware (especially GPUs,) and makes 
it both easier and faster to access or make use of subgraph-level optimizations 
that would otherwise be bounded by the compute limitations of the device. 

As a component under [Keras], PlaidML can accelerate training workloads with 
customized or automatically-generated Tile code. It works especially well on 
GPUs, and it doesn't require use of CUDA/cuDNN on Nvidia* hardware, while 
achieving comparable performance.

It works on all major operating systems: Linux, macOS, and Windows. 

# Contribution of this Fork
### Arm64 (aarch64) wheel building
The objective of this repository is to enable building PlaidML for ARM devices, concretely arm64v8 (aarch64) devices. The PlaidML version used is **v0.6.2**.

## Installation instructions
1. Download bazelisk for a linux_x86_64 (**linux_amd64**) device from [here](https://github.com/bazelbuild/bazelisk/releases/). I used this [version](https://github.com/bazelbuild/bazelisk/releases/download/v1.7.3/bazelisk-linux-amd64) 
2. Clone this repository `git clone https://github.com/DaniDeniz/plaidml.git; cd plaidml`
3. Create a Python3 virtual environment with the requirements described in [environment.yml](environment.yml). Activate the virtualenv. 
4. Inside the plaidml root, run the following command: `bazelisk build --config linux_arm_64v8 --verbose_failures //plaidml:wheel`. Note that we are running this on a linux_x86_64 device and we are crosscompiling the python wheel for the arm64 architecture.
5. Once it builds sucessfully, locate the wheel at the following directory: `bazel-bin/plaidml/wheel.pkg/dist/`. Transfer it to the arm64 device.
6. Install it on the arm64 device inside a python virtualenv `pip install -U plaidml-0.6.2*_aarch64.whl`
7. Install plaidml-keras: `pip install -U plaidml-keras==0.6.2`

This was tested on a Nvidia Jetson Nano. If you encounter any error check the official plaidml documentation.    

You can also download the corresponding wheel for arm64 from the [**0.6.2 arm64 release**](https://github.com/DaniDeniz/plaidml/releases/download/0.6.2_arm64/plaidml-0.6.2.dev0-py2.py3-none-linux_aarch64.whl)

## Getting started

For most platforms, getting started with accelerated deep learning is as easy as
running a few commands (assuming you have Python (v2 or v3) installed. If this 
doesn't work, see the [troubleshooting documentation](docs/troubleshooting.md):

    virtualenv plaidml
    source plaidml/bin/activate
    pip install plaidml-keras plaidbench

Choose which accelerator you'd like to use (many computers, especially laptops, have multiple):

    plaidml-setup

Next, try benchmarking MobileNet inference performance:

    plaidbench keras mobilenet

Or, try training MobileNet:

    plaidbench --batch-size 16 keras --train mobilenet


### Validated Hardware

Vertex.AI runs a comprehensive set of tests for each release against these hardware targets:

* AMD
    * R9 Nano
    * RX 480
    * Vega 10

* Intel
    * HD4000
    * HD Graphics 505

* NVIDIA
    * K80, GTX 780, GT 640M
    * GTX 1070, 1050

If you are using a hardware target not supported by PlaidML by default, such as Clover, 
check out the instructions at `building.md` to build a custom configuration for your hardware.

### Validated Networks

We support all of the Keras application networks from current versions of 2.x.
Validated networks are tested for performance and correctness as part of our 
continuous integration system.

* CNNs
   * Inception v3
   * ResNet50
   * VGG19
   * Xception
   * MobileNet
   * DenseNet
   * ShuffleNet

* LSTM
   * examples/imdb_lstm.py (from keras)

## Installation Instructions

We support a variety of operating systems and installation methods. 

* [Ubuntu Linux](docs/install.rst#ubuntu-linux)
* [macOS](docs/install.rst#macos)
* [Windows](docs/install.rst#windows)

## Demos and Related Projects

### Plaidbench

[Plaidbench](https://github.com/plaidml/plaidbench) is a performance testing suite designed to help users compare the performance of different cards and different frameworks.
  

### Hello VGG
One of the great things about Keras is how easy it is to play with state of the art networks. Here's all the code you
need to run VGG-19:

```python
#!/usr/bin/env python
import numpy as np
import os
import time

os.environ["KERAS_BACKEND"] = "plaidml.keras.backend"

import keras
import keras.applications as kapp
from keras.datasets import cifar10

(x_train, y_train_cats), (x_test, y_test_cats) = cifar10.load_data()
batch_size = 8
x_train = x_train[:batch_size]
x_train = np.repeat(np.repeat(x_train, 7, axis=1), 7, axis=2)
model = kapp.VGG19()
model.compile(optimizer='sgd', loss='categorical_crossentropy',
              metrics=['accuracy'])

print("Running initial batch (compiling tile program)")
y = model.predict(x=x_train, batch_size=batch_size)

# Now start the clock and run 10 batches
print("Timing inference...")
start = time.time()
for i in range(10):
    y = model.predict(x=x_train, batch_size=batch_size)
print("Ran in {} seconds".format(time.time() - start))

```

## Reporting Issues
Either open a ticket on [GitHub] or join our [slack workspace (#plaidml)](https://join.slack.com/t/ngraph/shared_invite/enQtNjY1Njk4OTczMzEyLWIyZjZkMDNiNzJlYWQ3MGIyZTg2NjRkODAyYWZlZWY5MmRiODdlNzVkMjcxNjNmNWEyZjNkMDVhMTgwY2IzOWQ).


[nGraph Compiler stack]: https://ngraph.nervanasys.com/docs/latest/
[Keras]: https://keras.io/
[GitHub]: https://github.com/plaidml/plaidml/issues
[plaidml-dev]: https://groups.google.com/forum/#!forum/plaidml-dev
