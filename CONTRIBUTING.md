# Contributing to DX12-Overlay

Thank you for your interest in contributing to DX12-Overlay! This document provides guidelines for contributing to the project.

## Code Quality Standards

### Resource Management
- **Always use `Microsoft::WRL::ComPtr`** for DirectX and DXGI objects
- Never use raw pointers for COM objects that need reference counting
- Ensure proper cleanup in destructors (RAII pattern)
- Resources should be allocated in constructors/Init and freed in destructors/Shutdown

### Error Handling
- **Check all HRESULT values** from DirectX API calls
- Use `DX12Utils::OutputHRError()` or `DX12Utils::CheckHR()` for error logging
- Provide meaningful error messages that help debugging
- Handle errors gracefully without crashing

### Performance
- Reuse command allocators and command lists across frames
- Cache descriptor heaps, PSOs, and root signatures
- Minimize state changes in render loops
- Avoid unnecessary resource barriers
- Profile changes with PIX or RenderDoc

### Code Style
- Follow the `.clang-format` configuration in the repository
- Use meaningful variable and function names
- Add comments for complex DirectX operations
- Use `camelCase` for local variables and function parameters
- Use `PascalCase` for class names and public methods
- Keep functions focused and under 100 lines when possible

### Documentation
- Add XML/Doxygen comments for public APIs
- Update README.md if adding new features
- Document performance implications of changes
- Add inline comments for non-obvious DirectX state management

## Pull Request Process

1. **Create a feature branch** from `main`
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make focused, incremental changes**
   - One logical change per commit
   - Write clear commit messages
   - Keep PRs small and reviewable

3. **Test your changes**
   - Build in both Debug and Release configurations
   - Run the application and verify functionality
   - Check for memory leaks in Debug builds
   - Test on different GPUs if possible

4. **Update documentation**
   - Update README.md for user-facing changes
   - Update PERFORMANCE.md for optimization changes
   - Add comments for complex code

5. **Format your code**
   ```bash
   clang-format -i *.cpp *.h
   ```

6. **Submit pull request**
   - Provide clear description of changes
   - Reference any related issues
   - Include before/after performance metrics if applicable

## DirectX 12 Best Practices

### Synchronization
- Use fences for CPU-GPU synchronization
- Per-frame fence values for tracking resources
- Avoid unnecessary `WaitForGPU()` calls
- Use waitable swap chain objects for frame pacing

### Resource Barriers
- Only transition resources when necessary
- Batch multiple barriers into single call
- Use correct Before/After states
- Set Subresource to ALL_SUBRESOURCES when appropriate

### Command Lists and Allocators
- Reset allocator only after GPU finishes with it
- Reuse command lists across frames
- Close command list before submission
- Don't record to the same list from multiple threads

### Descriptor Heaps
- Create once, reuse throughout lifetime
- Make shader-visible heaps large enough to avoid resizing
- Bind descriptor heap before using descriptors
- Use correct descriptor heap type (CBV_SRV_UAV, RTV, DSV, SAMPLER)

## Debugging Tips

### Debug Layer
Enable D3D12 debug layer in Debug builds:
```cpp
#ifdef _DEBUG
ComPtr<ID3D12Debug> debugController;
if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
}
#endif
```

### PIX Markers
Add PIX markers for frame sections:
```cpp
PIXBeginEvent(commandList, PIX_COLOR_INDEX(1), L"ImGui Rendering");
// ... rendering code ...
PIXEndEvent(commandList);
```

### Common Issues
- **Device Removed**: Check HRESULT, enable debug layer
- **Resource Barriers**: Verify Before/After states match actual usage
- **Fence Hangs**: Ensure Signal() is called before Wait()
- **Black Screen**: Check clear color alpha with DirectComposition

## Testing

### Manual Testing Checklist
- [ ] Application launches without errors
- [ ] Overlay is transparent (DirectComposition mode)
- [ ] UI renders correctly
- [ ] Hotkeys work (toggle, exit, rebind)
- [ ] FPS limiter functions properly
- [ ] Resizing works correctly
- [ ] No visible leaks (memory usage stable over time)
- [ ] Performance is acceptable (check FPS counter)

### Performance Testing
- Run for at least 1000 frames
- Record average frame time and std deviation
- Monitor CPU/GPU usage
- Compare with baseline measurements

## Code Review Standards

Pull requests will be reviewed for:
- **Correctness**: Does it work as intended?
- **Performance**: Does it maintain or improve performance?
- **Safety**: Are resources properly managed?
- **Style**: Does it follow coding conventions?
- **Tests**: Is it adequately tested?

## Questions or Issues?

- Open an issue for bugs or feature requests
- Start a discussion for design questions
- Check existing issues before creating new ones

## License

By contributing, you agree that your contributions will be licensed under the same license as the project.
