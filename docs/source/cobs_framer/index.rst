===========
COBS Framer
===========

Overview
========

Gantry makes use of `Consistent Overhead Byte Stuffing (COBS) <https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing>`_ encoding for data sent or received. It allows from explicitly framed packets while keeping the worst-case packet size small.

The Gantry COBS framer provides helpers for decoding and encoding COBS data. The decoder can be used to convert an incoming data stream into discrete decoded frames. The encoder is used to encode a payload into a COBS frame in a single pass.

Usage
=====

Decoding
--------

Allocate a ``net_buf`` pool large enough for the largest expected payload. The max size is of the decoded payload, there is no need to include the overhead of COBS encoding and the data is decoded in place as it arrives.

Initialize a COBS frame decoder instance for your transport and provide it with the ``net_buff`` pool.

.. code-block:: c

   static struct cobs_frame_decoder decoder;

   cobs_frame_decoder_init(&decoder, &rx_pool, on_frame, NULL);


Define a frame callback, then feed incoming bytes to the decoder as they arrive. The decoder fires the callback with a complete decoded frame once it sees the trailing COBS delimiter (``0x00``). The callback owns the ``net_buf`` and must call ``net_buf_unref`` when done.

.. code-block:: c

   static void on_frame(struct net_buf *buf, void *user_data)
   {
      // Process the data 
      net_buf_unref(buf);
   }

   void uart_rx_handler(const uint8_t *data, size_t len)
   {
      cobs_frame_decoder_feed(&decoder, data, len);
   }

Encoding
--------

Allocate a ``net_buf`` pool large enough to hold the largest possible payload encoded using COBS. Use :any:`COBS_ENCODE_MAX_SIZE` to calculate the worst-case encoded size.

To encode a ``net_buf`` using COBS, call :any:`cobs_frame_encode`. 

.. code-block:: c

   void send_data(struct net_buf *payload)
   {
       struct net_buf *out = NULL;
       int ret = cobs_frame_encode(&tx_pool, payload, &out);
       // Free the original net_buf
       net_buf_unref(payload);

       if (ret == 0) {
         // Transmit the data...

         // Free the encoded net_buf
         net_buf_unref(out);
       }
   }

Configuration
=============

.. code-block:: kconfig

   # Enable the COBS framer module
   GANTRY_COBS_FRAMER=y

API Reference
=============

.. doxygengroup:: cobs_framer
   :content-only:
