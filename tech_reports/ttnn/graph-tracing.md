# TT-NN Graph Trace
TT-NN provides a mechanism for tracing operations and memory activities in a neural network's execution.

Using this trace it is possible to analyze the operation even without executing it on the accelerator.
The output trace can then be processed to get a single number like Peak Memory Load or to print tabular data or visualize a call graph.

## 🪄 How to Use
Wrap any number of TT-NN calls with `GraphProcessor::begin_graph_capture` and `GraphProcessor::end_graph_capture` or use with any callable.
In the example below `ttnn::zeros` is not included in a trace, but `ttnn::add` is
https://github.com/tenstorrent/tt-metal/blob/4ae4ac3c30cd24ea27cbac8cc5811c90d077e9c0/tests/ttnn/unit_tests/gtests/test_graph_add.cpp#L50-L58

You can then analyze the trace with some of the provided utility functions
https://github.com/tenstorrent/tt-metal/blob/4ae4ac3c30cd24ea27cbac8cc5811c90d077e9c0/tests/ttnn/unit_tests/gtests/test_graph_add.cpp#L64-L66
or process it manually to extract whatever data in whatever format, like this table
```
           current_op                           event  total_cb  total_buffer                                                                                 info
0            ttnn::add                        begin_op         0       9011200                                                 {'inputs': '8', 'name': 'ttnn::add'}
1         ttnn::repeat                        begin_op         0       9011200                                              {'inputs': '2', 'name': 'ttnn::repeat'}
2         ttnn::repeat                 buffer_allocate         0      17203200    {'address': '753696', 'layout': 'INTERLEAVED', 'size': '8192000', 'type': 'DRAM'}
3         ttnn::repeat                 buffer_allocate         0      17209344   {'address': '1073735680', 'layout': 'INTERLEAVED', 'size': '6144', 'type': 'DRAM'}
4         ttnn::repeat        circular_buffer_allocate      4096      17209344    {'addr': '107360', 'core_range_set': '{[(x=0,y=0) - (x=7,y=7)]}', 'size': '4096'}
5         ttnn::repeat               buffer_deallocate      4096      17203200                               {'layout': 'INTERLEAVED', 'size': '0', 'type': 'DRAM'}
6         ttnn::repeat  circular_buffer_deallocate_all         0      17203200                                                                                   {}
7   ttnn::prim::binary                        begin_op         0      17203200                                       {'inputs': '10', 'name': 'ttnn::prim::binary'}
8   ttnn::prim::binary                 buffer_allocate         0      25395200   {'address': '1437728', 'layout': 'INTERLEAVED', 'size': '8192000', 'type': 'DRAM'}
9   ttnn::prim::binary                 buffer_allocate         0      25409536  {'address': '1073735680', 'layout': 'INTERLEAVED', 'size': '14336', 'type': 'DRAM'}
10  ttnn::prim::binary        circular_buffer_allocate      4096      25409536    {'addr': '107360', 'core_range_set': '{[(x=0,y=0) - (x=7,y=7)]}', 'size': '4096'}
11  ttnn::prim::binary        circular_buffer_allocate      8192      25409536    {'addr': '111456', 'core_range_set': '{[(x=0,y=0) - (x=7,y=7)]}', 'size': '4096'}
12  ttnn::prim::binary        circular_buffer_allocate     12288      25409536    {'addr': '115552', 'core_range_set': '{[(x=0,y=0) - (x=7,y=7)]}', 'size': '4096'}
13  ttnn::prim::binary               buffer_deallocate     12288      25395200                               {'layout': 'INTERLEAVED', 'size': '0', 'type': 'DRAM'}
14  ttnn::prim::binary  circular_buffer_deallocate_all         0      25395200                                                                                   {}
15           ttnn::add               buffer_deallocate         0      17203200                               {'layout': 'INTERLEAVED', 'size': '0', 'type': 'DRAM'}
16           ttnn::add  circular_buffer_deallocate_all         0      17203200                                                                                   {}
```
or a graph
![trace](https://github.com/user-attachments/assets/42501a1f-8354-4b3b-a5d9-707f30b23f4f)

## Trace Format
Trace is captured as a JSON and you can find the code producing it [here](https://github.com/tenstorrent/tt-metal/blob/main/ttnn/cpp/ttnn/graph/graph_processor.cpp).
Below you can find a detailed description of the schema.

### Node Types
The trace is represented as a directed graph, where each node corresponds to a specific operation or memory event. Below is an overview of the various types of nodes that can be present in the trace:

First, each node has these parameters

* `counter`: The unique identifier of the node within the graph.
* `node_type`: node type, available types are listed below
* `params`: the map of parameters \[mapping a string string property name to a string value\]
* `connections`: An array of connections to subsequent nodes.

### Node Connections
Each node in the graph maintains a list of connections to other nodes. These connections represent the flow of data and control through the various operations and memory events during the execution of the network.

### 1\. capture\_start
Marks the beginning of the graph capture process. This node is the root of the graph and does not have any parent nodes.

#### Parameters
* Empty, as this is a marker node.

#### Connections
* First element is next operation call
* Last element is the corresponding `capture_end`

### 2\. capture\_end
Marks the end of the graph capture process.

#### Parameters
* Empty, as this is a marker node.

### 3\. function\_start
Represents the beginning of a function or operation within the graph. This node captures details about the function name and the number of input parameters it received.

#### Parameters
* `inputs`: Number of input parameters.
* `name`: The name of the function or operation.

#### Connections
* Another op call, primitive op call, or a corresponding `function_end`

#### Functions types
* TT-NN operation: for example ttnn::add, ttnn::repeat
* TT-NN primitive operation: for example ttnn::prim::binary (if it uses TMP infra) or ttnn::prim::old\_infra\_device\_operation (if not)
* TT-NN device operation: name “Device Operation” (should change in future)
* Unregistered, but manually tracked functions: (will change)
  create\_device\_tensor, Tensor::to, Tensor::cpu, Tensor::cpu\_sharded, Tensor::print, Tensor::pad, Tensor::unpad, Tensor::reshape
  tt::tt\_metal::detail::convert\_python\_tensor\_to\_tt\_tensor, tt::tt\_metal::detail::convert\_tt\_tensor\_to\_torch\_tensor

### 4\. function\_end
Marks the end of a function or operation. This node is paired with the corresponding `begin_function` node and records the outputs or final state of the function.

#### Parameters
* `name`: The name of the function or operation.

#### Connections
* Output tensor/s
* Next control flow node (potentially `capture_end` node)

### 5\. buffer
Represents the allocation of a memory buffer. This node records details about the buffer's size, type (e.g., DRAM or L1 cache), and layout.

#### Parameters
* `size`: The size of the buffer in bytes.
* `type`: The type of memory (e.g., "DRAM", "L1").
* `layout`: The memory layout (e.g., "INTERLEAVED", "SINGLE\_BANK").

#### Connections
* Single element in the connection list specifies the associated Tensor ID

### 6\. buffer\_allocate
Denotes the allocation of a buffer in memory. This node captures the specifics of the memory allocation event, including the buffer's address and type.

#### Parameters
* `size`: The size of the buffer in bytes.
* `address`: The memory address of the buffer.
* `type`: The type of memory (e.g., "DRAM", "L1").
* `layout`: The memory layout.

#### Connections
* Single element in the connections list specifies the allocated buffer ID

### 7\. buffer\_deallocate
Represents the deallocation of a buffer from memory. This node records the details of the buffer being deallocated.

#### Parameters
* `size`: The size of the buffer in bytes.
* `type`: The type of memory (e.g., "DRAM", "L1").
* `layout`: The memory layout.

#### Connections
* Single element in the connections list specifies the deallocated buffer ID

### 8\. circular\_buffer\_allocate
Represents the allocation of a circular buffer, typically used in handling streaming data or multi-buffering strategies. This node captures details like the core range set involved and the buffer size.

#### Parameters
* `size`: The size of the circular buffer in bytes.
* `address`: The memory address associated with the buffer.
* `core_range_set`: The range of cores involved in the circular buffer.

#### Connections
Usually empty

### 9\. circular\_buffer\_deallocate\_all
Marks the deallocation of all circular buffers. This is a bulk operation and is connected to all circular buffer nodes that are being deallocated.

#### Parameters
Empty, as this operation deallocates all circular buffers.

#### Connections
Usually empty

### 10\. tensor
Represents a tensor in the graph. This node captures the tensor's shape and is connected to the memory buffer it uses, if applicable.
`[#]` means that each tensor is indexed, and instead of \# in real trace you will see an id

#### Parameters
* `tensor_id`: The identified of the tensor.
* `shape`: The shape of the tensor.

#### Connections
Usually specifies function\_start of a function where given tensor is passed as an argument/used

## Operation Dispatching
When run in `NO_DISPATCH` run mode, real allocations do not happen, so trace collection does not have side effects on the allocator state.
You can pass unrealistically big tensors in this mode and unless an operation does upper limit validation, you still can collect the trace.
In this mode trace collection is faster because ops are dispatched to the device.

When run in the `NORMAL` mode, memory can be fragmented, which can lead to a different trace and you see real addresses where everything is allocated.

## Python
Tracing is available through Python too
https://github.com/tenstorrent/tt-metal/blob/4ae4ac3c30cd24ea27cbac8cc5811c90d077e9c0/tests/ttnn/unit_tests/test_graph_capture.py#L21-L25

Here is a sample code to print a table from the beginning of this document

```py
def process_allocations(graph):
   df = pd.DataFrame(columns=['current_op', 'event', 'total_cb', 'total_buffer', 'info'])

   cur_op = []
   total_cb = 0
   total_buffer = 0
   tensors = set()
   i = 1 # lets skip initial node
   while i < len(graph):
       params = ''
       v = graph[i]
       params = v.params
       print(v, len(df))
       i += 1
       if v.node_type == 'function_start':
           if len(cur_op) == 0:
               #entring first op, lets get all input tensors
               while i < len(graph):
                   print(graph[i], len(df))
                   if graph[i].node_type == 'buffer':
                       total_buffer += int(graph[i].params['size'])
                       i += 1
                   elif graph[i].node_type == 'tensor':
                       i += 1
                   else:
                       break
           name = v.params['name']
           if name == "ttnn::prim::old_infra_device_operation":
               name = "ttnn::prim::old_infra_op"
           cur_op.append(name)
       if v.node_type == 'circular_buffer_allocate':
           total_cb += int(v.params['size'])
       if v.node_type == 'circular_buffer_deallocate_all':
           total_cb = 0
       if v.node_type == 'buffer_allocate':
           total_buffer += int(v.params['size'])
       if v.node_type == 'function_end':
           cur_op.pop()
           #continue
       if v.node_type == 'tensor':
           continue
       if v.node_type == 'buffer_deallocate':
           total_buffer -= int(graph[v.connections[0]].params['size'])
       if v.node_type == 'buffer':
           continue
       if len(cur_op) > 0:
           data =  {'current_op': cur_op[-1], 'event' : v.node_type, 'total_cb': total_cb, 'total_buffer': total_buffer, 'info' : params}
           df.loc[len(df)] = data
   return df
```

## Sample Trace

This is a sample trace of running `ttnn::add(Shape\[1, 1, 32, 32\], Shape\[4, 1, 32, 32\])`.
This setup requires to broadcast the first tensor, so trace contains a call to ttnn::repeat.
High level call stack here is:

```
ttnn::add
ttnn::repeat
ttnn::prim::old_infra_device_operation (calling ttnn primitive operation)
Device Operation (dispatching device operation)
create_device_tensor (creates intermediate output for ttnn::repeat)
ttnn::prim::binary (calling ttnn primitive operation)
Device Operation (dispatching device operation)
create_device_tensor (creates final output)
```

And you can see when each Buffer and CB is allocated / deallocated.

### PrettyPrint

```
Capture Start
Begin: tt::tt_metal::detail::convert_python_tensor_to_tt_tensor
End:   tt::tt_metal::detail::convert_python_tensor_to_tt_tensor
Add Tensor: 0
Begin: ttnn::to_layout
    Begin: Tensor::reshape
    End:   Tensor::reshape
    Add Tensor: 1
    Begin: Tensor::pad
    End:   Tensor::pad
    Add Tensor: 2
    Begin: Tensor::to
    End:   Tensor::to
    Add Tensor: 3
End:   ttnn::to_layout
Begin: Tensor::to
    Add Device Buffer
    Allocate Device Buffer
End:   Tensor::to
Add Tensor: 4
Begin: ttnn::add
    Begin: Tensor::to
        Add Tensor: -1
        Add Device Buffer
        Allocate Device Buffer
    End:   Tensor::to
    Add Tensor: 5
    Begin: ttnn::prim::binary
        Begin: BinaryDeviceOperation
            Begin: tt::tt_metal::create_device_tensor
                Add Device Buffer
                Allocate Device Buffer
            End:   tt::tt_metal::create_device_tensor
            Add Tensor: 6
            Add Tensor: 6
            Add Device Buffer
            Allocate Device Buffer
            Allocate Device Buffer
            Allocate Device Buffer
            Allocate Device Buffer
            Deallocate Device Buffer
        End:   BinaryDeviceOperation
        Add Tensor: 7
    End:   ttnn::prim::binary
    Deallocate Device Buffer
End:   ttnn::add
Begin: Tensor::cpu
End:   Tensor::cpu
Add Tensor: 8
Begin: Tensor::to
End:   Tensor::to
Add Tensor: 9
Begin: tt::tt_metal::detail::convert_tt_tensor_to_torch_tensor
End:   tt::tt_metal::detail::convert_tt_tensor_to_torch_tensor
Deallocate Device Buffer
Deallocate Device Buffer
```

### Visualizer

![visualizer](https://github.com/user-attachments/assets/03df00c6-4902-416d-a26a-6ffe874537a5)


## Raw JSON
```
[
    {
        "connections": [
            1,
            32
        ],
        "counter": 0,
        "node_type": "capture_start",
        "params": {}
    },
    {
        "connections": [
            3,
            5,
            6,
            18,
            30,
            31
        ],
        "counter": 1,
        "node_type": "function_start",
        "params": {
            "inputs": "2",
            "name": "ttnn::add"
        }
    },
    {
        "connections": [
            1,
            18
        ],
        "counter": 2,
        "node_type": "tensor",
        "params": {
            "shape": "ttnn.Shape([4, 3, 32, 32])"
        }
    },
    {
        "connections": [
            2,
            2
        ],
        "counter": 3,
        "name": "buffer",
        "params": {
            "layout": "INTERLEAVED",
            "size": "24576",
            "type": "L1"
        }
    },
    {
        "connections": [
            1,
            6
        ],
        "counter": 4,
        "name": "tensor[1]",
        "params": {
            "shape": "ttnn.Shape([1, 3, 32, 32])"
        }
    },
    {
        "connections": [
            4,
            4
        ],
        "counter": 5,
        "name": "buffer",
        "params": {
            "layout": "INTERLEAVED",
            "size": "6144",
            "type": "L1"
        }
    },
    {
        "connections": [
            7,
            17
        ],
        "counter": 6,
        "name": "function_start",
        "params": {
            "inputs": "2",
            "name": "ttnn::repeat"
        }
    },
    {
        "connections": [
            8,
            16
        ],
        "counter": 7,
        "name": "function_start",
        "params": {
            "inputs": "5",
            "name": "ttnn::prim::old_infra_device_operation"
        }
    },
    {
        "connections": [
            9,
            14,
            15
        ],
        "counter": 8,
        "name": "function_start",
        "params": {
            "inputs": "2",
            "name": "Device Operation"
        }
    },
    {
        "connections": [
            10,
            11,
            12
        ],
        "counter": 9,
        "name": "function_start",
        "params": {
            "inputs": "5",
            "name": "create_device_tensor"
        }
    },
    {
        "connections": [
            13,
            13,
            13,
            13,
            13
        ],
        "counter": 10,
        "name": "buffer",
        "params": {
            "layout": "INTERLEAVED",
            "size": "24576",
            "type": "L1"
        }
    },
    {
        "connections": [
            10
        ],
        "counter": 11,
        "name": "buffer_allocate",
        "params": {
            "address": "1953396066",
            "layout": "INTERLEAVED",
            "size": "24576",
            "type": "L1"
        }
    },
    {
        "connections": [
            13
        ],
        "counter": 12,
        "name": "function_end",
        "params": {
            "name": "create_device_tensor"
        }
    },
    {
        "connections": [
            18
        ],
        "counter": 13,
        "name": "tensor[2]",
        "params": {
            "shape": "ttnn.Shape([4, 3, 32, 32])"
        }
    },
    {
        "connections": [],
        "counter": 14,
        "name": "circular_buffer_allocate",
        "params": {
            "address": "0",
            "core_range_set": "{[(x=0,y=0) - (x=0,y=7)], [(x=1,y=0) - (x=1,y=3)]}",
            "size": "4096"
        }
    },
    {
        "connections": [
            13
        ],
        "counter": 15,
        "name": "function_end",
        "params": {
            "name": "Device Operation"
        }
    },
    {
        "connections": [
            13
        ],
        "counter": 16,
        "name": "function_end",
        "params": {
            "name": "ttnn::prim::old_infra_device_operation"
        }
    },
    {
        "connections": [
            13,
            18
        ],
        "counter": 17,
        "name": "function_end",
        "params": {
            "name": "ttnn::repeat"
        }
    },
    {
        "connections": [
            19,
            29
        ],
        "counter": 18,
        "name": "function_start",
        "params": {
            "inputs": "10",
            "name": "ttnn::prim::binary"
        }
    },
    {
        "connections": [
            20,
            25,
            26,
            27,
            28
        ],
        "counter": 19,
        "name": "function_start",
        "params": {
            "inputs": "2",
            "name": "Device Operation"
        }
    },
    {
        "connections": [
            21,
            22,
            23
        ],
        "counter": 20,
        "name": "function_start",
        "params": {
            "inputs": "5",
            "name": "create_device_tensor"
        }
    },
    {
        "connections": [
            24,
            24,
            24,
            24
        ],
        "counter": 21,
        "name": "buffer",
        "params": {
            "layout": "INTERLEAVED",
            "size": "24576",
            "type": "L1"
        }
    },
    {
        "connections": [
            21
        ],
        "counter": 22,
        "name": "buffer_allocate",
        "params": {
            "address": "0",
            "layout": "INTERLEAVED",
            "size": "24576",
            "type": "L1"
        }
    },
    {
        "connections": [
            24
        ],
        "counter": 23,
        "name": "function_end",
        "params": {
            "name": "create_device_tensor"
        }
    },
    {
        "connections": [],
        "counter": 24,
        "name": "tensor[3]",
        "params": {
            "shape": "ttnn.Shape([4, 3, 32, 32])"
        }
    },
    {
        "connections": [],
        "counter": 25,
        "name": "circular_buffer_allocate",
        "params": {
            "address": "0",
            "core_range_set": "{[(x=0,y=0) - (x=7,y=7)]}",
            "size": "4096"
        }
    },
    {
        "connections": [],
        "counter": 26,
        "name": "circular_buffer_allocate",
        "params": {
            "address": "0",
            "core_range_set": "{[(x=0,y=0) - (x=7,y=7)]}",
            "size": "4096"
        }
    },
    {
        "connections": [],
        "counter": 27,
        "name": "circular_buffer_allocate",
        "params": {
            "address": "0",
            "core_range_set": "{[(x=0,y=0) - (x=7,y=7)]}",
            "size": "4096"
        }
    },
    {
        "connections": [
            24
        ],
        "counter": 28,
        "name": "function_end",
        "params": {
            "name": "Device Operation"
        }
    },
    {
        "connections": [
            24
        ],
        "counter": 29,
        "name": "function_end",
        "params": {
            "name": "ttnn::prim::binary"
        }
    },
    {
        "connections": [
            10
        ],
        "counter": 30,
        "name": "buffer_deallocate",
        "params": {
            "layout": "INTERLEAVED",
            "size": "0",
            "type": "L1"
        }
    },
    {
        "connections": [
            24,
            33
        ],
        "counter": 31,
        "name": "function_end",
        "params": {
            "name": "ttnn::add"
        }
    },
    {
        "connections": [
            21
        ],
        "counter": 32,
        "name": "buffer_deallocate",
        "params": {
            "layout": "INTERLEAVED",
            "size": "0",
            "type": "L1"
        }
    },
    {
        "connections": [],
        "counter": 33,
        "name": "capture_end",
        "params": {}
    }
]
```
