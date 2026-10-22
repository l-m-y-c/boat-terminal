import 'package:flutter/material.dart';

/// Horizontal swipe between a fixed set of tabs that wraps round both ways:
/// swiping left goes to the next tab (after the last comes the first),
/// swiping right goes to the previous one.
///
/// Built on an effectively endless [PageView], so the page follows the
/// finger and settles with the standard animation. The parent owns [index]
/// (for the bottom bar); changing it from outside animates the short way
/// round.
class WrapTabPager extends StatefulWidget {
  const WrapTabPager({
    super.key,
    required this.count,
    required this.index,
    required this.onChanged,
    required this.builder,
  });

  final int count;
  final int index;
  final ValueChanged<int> onChanged;
  final Widget Function(BuildContext context, int index) builder;

  @override
  State<WrapTabPager> createState() => _WrapTabPagerState();
}

class _WrapTabPagerState extends State<WrapTabPager> {
  // Far enough from 0 that nobody swipes to the start.
  static const _origin = 10000;
  late final PageController _controller;
  late int _page;

  @override
  void initState() {
    super.initState();
    _page = _origin * widget.count + widget.index;
    _controller = PageController(initialPage: _page);
  }

  int _mod(int page) => page % widget.count;

  @override
  void didUpdateWidget(WrapTabPager old) {
    super.didUpdateWidget(old);
    if (widget.index == _mod(_page)) return;
    var delta = (widget.index - _mod(_page)) % widget.count;
    if (delta > widget.count / 2) delta -= widget.count;
    final target = _page + delta;
    _page = target;
    if (_controller.hasClients) {
      _controller.animateToPage(
        target,
        duration: const Duration(milliseconds: 280),
        curve: Curves.easeOutCubic,
      );
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return PageView.builder(
      key: const ValueKey('tab-pager'),
      controller: _controller,
      onPageChanged: (page) {
        _page = page;
        final i = _mod(page);
        if (i != widget.index) widget.onChanged(i);
      },
      itemBuilder: (context, page) => widget.builder(context, _mod(page)),
    );
  }
}
