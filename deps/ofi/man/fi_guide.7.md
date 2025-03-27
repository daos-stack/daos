---
layout: page
title: fi_guide(7)
tagline: Libfabric Programmer's Guide
---
{% include JB/setup %}

# NAME

fi_guide \- libfabric programmer's guide

# OVERVIEW

libfabric is a communication library framework designed to meet the
performance and scalability requirements of high-performance computing (HPC)
applications.  libfabric defines communication interfaces that enable a
tight semantic map between applications and underlying network services.
Specifically, libfabric software interfaces have been co-designed with
network hardware providers and application developers, with a focus on
the needs of HPC users.

This guide describes the libfabric architecture and interfaces.  Due to
the length of the guide, it has been broken into multiple pages.  These
sections are:

*Introduction [`fi_intro`(7)](fi_intro.7.html)*
: This section provides insight into the motivation for the libfabric
  design and underlying networking features that are being exposed through
  the API.

*Architecture [`fi_arch`(7)](fi_arch.7.html)*
: This describes the exposed architecture of libfabric, including the
  object-model and their related operations

*Setup [`fi_setup`(7)](fi_setup.7.html)*
: This provides basic bootstrapping and setup for using the libfabric API.
