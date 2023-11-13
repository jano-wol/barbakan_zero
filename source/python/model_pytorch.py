import math
import numpy as np
import torch
import torch.nn
import torch.nn.functional
import torch.nn.init
import packaging
import packaging.version
from path_util import get_test_data_folder
from typing import List, Dict, Optional

import modelconfigs

EXTRA_SCORE_DISTR_RADIUS = 60


def static_vars(**kwargs):
    def decorate(func):
        for k in kwargs:
            setattr(func, k, kwargs[k])
        return func

    return decorate


def act(activation, inplace=False):
    if activation == "relu":
        return torch.nn.ReLU(inplace=inplace)
    if activation == "elu":
        return torch.nn.ELU(inplace=inplace)
    if activation == "mish":
        return torch.nn.Mish(inplace=inplace)
    if activation == "gelu":
        return torch.nn.GELU(inplace=inplace)
    if activation == "hardswish":
        if packaging.version.parse(torch.__version__) > packaging.version.parse("1.6.0"):
            return torch.nn.Hardswish(inplace=inplace)
        else:
            return torch.nn.Hardswish()
    if activation == "identity":
        return torch.nn.Identity()
    assert False, f"Unknown activation name: {activation}"


def compute_gain(activation):
    if activation == "relu" or activation == "hardswish":
        gain = math.sqrt(2.0)
    elif activation == "elu":
        gain = math.sqrt(1.55052)
    elif activation == "mish":
        gain = math.sqrt(2.210277)
    elif activation == "gelu":
        gain = math.sqrt(2.351718)
    elif activation == "identity":
        gain = 1.0
    else:
        assert False, f"Unknown activation name: {activation}"
    return gain


def init_weights(tensor, activation, scale, fan_tensor=None):
    gain = compute_gain(activation)

    if fan_tensor is not None:
        (fan_in, _) = torch.nn.init._calculate_fan_in_and_fan_out(fan_tensor)
    else:
        (fan_in, _) = torch.nn.init._calculate_fan_in_and_fan_out(tensor)

    target_std = scale * gain / math.sqrt(fan_in)
    # Multiply slightly since we use truncated normal
    std = target_std / 0.87962566103423978
    if std < 1e-10:
        tensor.fill_(0.0)
    else:
        torch.nn.init.trunc_normal_(tensor, mean=0.0, std=std, a=-2.0 * std, b=2.0 * std)


class SoftPlusWithGradientFloorFunction(torch.autograd.Function):
    """
    Same as softplus, except on backward pass, we never let the gradient decrease below grad_floor.
    Equivalent to having a dynamic learning rate depending on stop_grad(x) where x is the input.
    If square, then also squares the result while halving the input, and still also keeping the same gradient.
    """

    @staticmethod
    def forward(ctx, x: torch.Tensor, grad_floor: float, square: bool):
        ctx.save_for_backward(x)
        ctx.grad_floor = grad_floor  # grad_floor is not a tensor
        if square:
            return torch.square(torch.nn.functional.softplus(0.5 * x))
        else:
            return torch.nn.functional.softplus(x)

    @staticmethod
    def backward(ctx, grad_output: torch.Tensor):
        (x,) = ctx.saved_tensors
        grad_floor = ctx.grad_floor
        grad_x = None
        grad_grad_floor = None
        grad_square = None
        if ctx.needs_input_grad[0]:
            grad_x = grad_output * (grad_floor + (1.0 - grad_floor) / (1.0 + torch.exp(-x)))
        return grad_x, grad_grad_floor, grad_square


class BiasMask(torch.nn.Module):
    def __init__(
            self,
            c_in,
            config: modelconfigs.ModelConfig,
            is_after_batchnorm: bool = False,
    ):
        super(BiasMask, self).__init__()
        self.c_in = c_in
        self.beta = torch.nn.Parameter(torch.zeros(1, c_in, 1, 1))
        self.is_after_batchnorm = is_after_batchnorm
        self.scale = None

    def set_scale(self, scale: Optional[float]):
        self.scale = scale

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        if self.is_after_batchnorm:
            reg_dict["output_noreg"].append(self.beta)
        else:
            reg_dict["noreg"].append(self.beta)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        pass

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        pass

    def forward(self, x, mask, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum: scalar

        Returns: NCHW
        """
        if self.scale is not None:
            return (x * self.scale + self.beta) * mask
        else:
            return (x + self.beta) * mask


class NormMask(torch.nn.Module):
    def __init__(
            self,
            c_in,
            config: modelconfigs.ModelConfig,
            fixup_use_gamma: bool,
            force_use_gamma: bool = False,
            is_last_batchnorm: bool = False,
    ):
        """Various kinds of normalization.

        bnorm - batch norm
        brenorm - batch renorm
        fixup - fixup initialization https://arxiv.org/abs/1901.09321
        fixscale - fixed scaling initialization. Normalization layers simply multiply a constant scalar according
          to what batchnorm *would* do if all inputs were unit variance and all linear layers or convolutions
          preserved variance.
        fixbrenorm - fixed scaling normalization PLUS batch renorm.
        fixscaleonenorm - fixed scaling normalization PLUS only have one batch norm layer in the entire net, at the end of the residual trunk.
        """

        super(NormMask, self).__init__()
        self.norm_kind = config["norm_kind"]
        self.epsilon = config["bnorm_epsilon"]
        self.running_avg_momentum = config["bnorm_running_avg_momentum"]
        self.fixup_use_gamma = fixup_use_gamma
        self.is_last_batchnorm = is_last_batchnorm
        self.use_gamma = (
                ("bnorm_use_gamma" in config and config["bnorm_use_gamma"]) or
                ((
                         self.norm_kind == "fixup" or self.norm_kind == "fixscale" or self.norm_kind == "fixscaleonenorm") and fixup_use_gamma) or
                force_use_gamma
        )
        self.c_in = c_in

        self.scale = None
        self.gamma = None
        if self.norm_kind == "bnorm" or (self.norm_kind == "fixscaleonenorm" and self.is_last_batchnorm):
            self.is_using_batchnorm = True
            if self.use_gamma:
                self.gamma = torch.nn.Parameter(torch.ones(1, c_in, 1, 1))
            self.beta = torch.nn.Parameter(torch.zeros(1, c_in, 1, 1))
            self.register_buffer(
                "running_mean", torch.zeros(c_in, dtype=torch.float)
            )
            self.register_buffer(
                "running_std", torch.ones(c_in, dtype=torch.float)
            )
        elif self.norm_kind == "brenorm" or self.norm_kind == "fixbrenorm":
            self.is_using_batchnorm = True
            if self.use_gamma:
                self.gamma = torch.nn.Parameter(torch.ones(1, c_in, 1, 1))
            self.beta = torch.nn.Parameter(torch.zeros(1, c_in, 1, 1))
            self.register_buffer(
                "running_mean", torch.zeros(c_in, dtype=torch.float)
            )
            self.register_buffer(
                "running_std", torch.ones(c_in, dtype=torch.float)
            )
            self.register_buffer(
                "renorm_running_mean", torch.zeros(c_in, dtype=torch.float)
            )
            self.register_buffer(
                "renorm_running_std", torch.ones(c_in, dtype=torch.float)
            )
            self.register_buffer(
                "renorm_upper_rclippage", torch.zeros((), dtype=torch.float)
            )
            self.register_buffer(
                "renorm_lower_rclippage", torch.zeros((), dtype=torch.float)
            )
            self.register_buffer(
                "renorm_dclippage", torch.zeros((), dtype=torch.float)
            )

        elif self.norm_kind == "fixup" or self.norm_kind == "fixscale" or (
                self.norm_kind == "fixscaleonenorm" and not self.is_last_batchnorm):
            self.is_using_batchnorm = False
            self.beta = torch.nn.Parameter(torch.zeros(1, c_in, 1, 1))
            if self.use_gamma:
                self.gamma = torch.nn.Parameter(torch.ones(1, c_in, 1, 1))
        else:
            assert False, f"Unimplemented norm_kind: {self.norm_kind}"

    def set_scale(self, scale: Optional[float]):
        self.scale = scale

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        if self.is_last_batchnorm:
            if self.gamma is not None:
                reg_dict["output"].append(self.gamma)
            reg_dict["output_noreg"].append(self.beta)
        else:
            if self.gamma is not None:
                reg_dict["normal_gamma"].append(self.gamma)
            reg_dict["noreg"].append(self.beta)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.renorm_avg_momentum = renorm_avg_momentum
        self.rmax = rmax
        self.dmax = dmax

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        upper_rclippage.append(self.renorm_upper_rclippage.cpu().item())
        lower_rclippage.append(self.renorm_lower_rclippage.cpu().item())
        dclippage.append(self.renorm_dclippage.cpu().item())

    def _compute_bnorm_values(self, x, mask, mask_sum: float):
        # This is the mean, computed only over exactly the areas of the mask, weighting each spot equally,
        # even across different elements in the batch that might have different board sizes.
        mean = torch.sum(x * mask, dim=(0, 2, 3), keepdim=True) / mask_sum
        zeromean_x = x - mean
        # Similarly, the variance computed exactly only over those spots
        var = torch.sum(torch.square(zeromean_x * mask), dim=(0, 2, 3), keepdim=True) / mask_sum
        std = torch.sqrt(var + self.epsilon)
        return zeromean_x, mean, std

    def apply_gamma_beta_scale_mask(self, x, mask):
        if self.scale is not None:
            if self.gamma is not None:
                return (x * (self.gamma * self.scale) + self.beta) * mask
            else:
                return (x * self.scale + self.beta) * mask
        else:
            if self.gamma is not None:
                return (x * self.gamma + self.beta) * mask
            else:
                return (x + self.beta) * mask

    def forward(self, x, mask, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum: scalar

        Returns: NCHW
        """

        if self.norm_kind == "bnorm" or (self.norm_kind == "fixscaleonenorm" and self.is_last_batchnorm):
            assert x.shape[1] == self.c_in
            if self.training:
                zeromean_x, mean, std = self._compute_bnorm_values(x, mask, mask_sum)

                detached_mean = mean.view(self.c_in).detach()
                detached_std = std.view(self.c_in).detach()
                with torch.no_grad():
                    self.running_mean += self.running_avg_momentum * (detached_mean - self.running_mean)
                    self.running_std += self.running_avg_momentum * (detached_std - self.running_std)

                return self.apply_gamma_beta_scale_mask(zeromean_x / std, mask)
            else:
                return self.apply_gamma_beta_scale_mask(
                    (x - self.running_mean.view(1, self.c_in, 1, 1)) / self.running_std.view(1, self.c_in, 1, 1), mask)

        elif self.norm_kind == "brenorm" or self.norm_kind == "fixbrenorm":
            assert x.shape[1] == self.c_in
            if self.training:
                zeromean_x, mean, std = self._compute_bnorm_values(x, mask, mask_sum)

                detached_mean = mean.view(self.c_in).detach()
                detached_std = std.view(self.c_in).detach()
                with torch.no_grad():
                    unclipped_r = detached_std / self.renorm_running_std
                    unclipped_d = (detached_mean - self.renorm_running_mean) / self.renorm_running_std
                    r = unclipped_r.clamp(1.0 / self.rmax, self.rmax)
                    d = unclipped_d.clamp(-self.dmax, self.dmax)

                    self.renorm_running_mean += self.renorm_avg_momentum * (detached_mean - self.renorm_running_mean)
                    self.renorm_running_std += self.renorm_avg_momentum * (detached_std - self.renorm_running_std)
                    self.running_mean += self.running_avg_momentum * (detached_mean - self.running_mean)
                    self.running_std += self.running_avg_momentum * (detached_std - self.running_std)

                    upper_rclippage = torch.mean(torch.nn.functional.relu(torch.log(unclipped_r / r)))
                    lower_rclippage = torch.mean(torch.nn.functional.relu(-torch.log(unclipped_r / r)))
                    dclippage = torch.mean(torch.abs(unclipped_d - d))
                    self.renorm_upper_rclippage += 0.01 * (upper_rclippage - self.renorm_upper_rclippage)
                    self.renorm_lower_rclippage += 0.01 * (lower_rclippage - self.renorm_lower_rclippage)
                    self.renorm_dclippage += 0.01 * (dclippage - self.renorm_dclippage)

                if self.rmax > 1.00000001 or self.dmax > 0.00000001:
                    return self.apply_gamma_beta_scale_mask(
                        zeromean_x / std * r.detach().view(1, self.c_in, 1, 1) + d.detach().view(1, self.c_in, 1, 1),
                        mask)
                else:
                    return self.apply_gamma_beta_scale_mask(zeromean_x / std, mask)

            else:
                return self.apply_gamma_beta_scale_mask(
                    (x - self.running_mean.view(1, self.c_in, 1, 1)) / self.running_std.view(1, self.c_in, 1, 1), mask)

        elif self.norm_kind == "fixup" or self.norm_kind == "fixscale" or (
                self.norm_kind == "fixscaleonenorm" and not self.is_last_batchnorm):
            return self.apply_gamma_beta_scale_mask(x, mask)

        else:
            assert False


class KataGPool(torch.nn.Module):
    def __init__(self):
        super(KataGPool, self).__init__()

    def forward(self, x, mask, mask_sum_hw):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111

        Returns: NC11
        """
        mask_sum_hw_sqrt_offset = torch.sqrt(mask_sum_hw) - 14.0

        layer_mean = torch.sum(x, dim=(2, 3), keepdim=True, dtype=torch.float32) / mask_sum_hw
        # All activation functions we use right now are always greater than -1.0, and map 0 -> 0.
        # So off-board areas will equal 0, and then this max is mask-safe if we assign -1.0 to off-board areas.
        (layer_max, _argmax) = torch.max((x + (mask - 1.0)).view(x.shape[0], x.shape[1], -1).to(torch.float32), dim=2)
        layer_max = layer_max.view(x.shape[0], x.shape[1], 1, 1)

        out_pool1 = layer_mean
        out_pool2 = layer_mean * (mask_sum_hw_sqrt_offset / 10.0)
        out_pool3 = layer_max

        out = torch.cat((out_pool1, out_pool2, out_pool3), dim=1)
        return out


class KataValueHeadGPool(torch.nn.Module):
    def __init__(self):
        super(KataValueHeadGPool, self).__init__()

    def forward(self, x, mask, mask_sum_hw):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111

        Returns: NC11
        """
        mask_sum_hw_sqrt_offset = torch.sqrt(mask_sum_hw) - 14.0

        layer_mean = torch.sum(x, dim=(2, 3), keepdim=True, dtype=torch.float32) / mask_sum_hw

        out_pool1 = layer_mean
        out_pool2 = layer_mean * (mask_sum_hw_sqrt_offset / 10.0)
        out_pool3 = layer_mean * ((mask_sum_hw_sqrt_offset * mask_sum_hw_sqrt_offset) / 100.0 - 0.1)

        out = torch.cat((out_pool1, out_pool2, out_pool3), dim=1)
        return out


class KataConvAndGPool(torch.nn.Module):
    def __init__(self, c_in, c_out, c_gpool, config, activation):
        super(KataConvAndGPool, self).__init__()
        self.norm_kind = config["norm_kind"]
        self.activation = activation
        self.conv1r = torch.nn.Conv2d(c_in, c_out, kernel_size=3, padding="same", bias=False)
        self.conv1g = torch.nn.Conv2d(c_in, c_gpool, kernel_size=3, padding="same", bias=False)
        self.normg = NormMask(
            c_gpool,
            config=config,
            fixup_use_gamma=False,
        )
        self.actg = act(self.activation, inplace=True)
        self.gpool = KataGPool()
        self.linear_g = torch.nn.Linear(3 * c_gpool, c_out, bias=False)

    def initialize(self, scale):
        # Scaling so that variance on the r and g branches adds up to 1.0
        r_scale = 0.8
        g_scale = 0.6
        if self.norm_kind == "fixup" or self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
            init_weights(self.conv1r.weight, self.activation, scale=scale * r_scale)
            init_weights(self.conv1g.weight, self.activation, scale=math.sqrt(scale) * math.sqrt(g_scale))
            init_weights(self.linear_g.weight, self.activation, scale=math.sqrt(scale) * math.sqrt(g_scale))
        else:
            init_weights(self.conv1r.weight, self.activation, scale=scale * r_scale)
            init_weights(self.conv1g.weight, self.activation, scale=math.sqrt(scale) * 1.0)
            init_weights(self.linear_g.weight, self.activation, scale=math.sqrt(scale) * g_scale)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        reg_dict["normal"].append(self.conv1r.weight)
        reg_dict["normal"].append(self.conv1g.weight)
        self.normg.add_reg_dict(reg_dict)
        reg_dict["normal"].append(self.linear_g.weight)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.normg.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.normg.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        out = x
        outr = self.conv1r(out)
        outg = self.conv1g(out)

        outg = self.normg(outg, mask=mask, mask_sum=mask_sum)
        outg = self.actg(outg)
        outg = self.gpool(outg, mask=mask, mask_sum_hw=mask_sum_hw).squeeze(-1).squeeze(-1)
        outg = self.linear_g(outg).unsqueeze(-1).unsqueeze(-1)

        out = outr + outg
        return out


class KataConvAndAttentionPool(torch.nn.Module):
    def __init__(self, c_in, c_out, c_gpool, config, activation):
        super(KataConvAndAttentionPool, self).__init__()
        self.norm_kind = config["norm_kind"]
        self.c_gpool = c_gpool
        self.c_apheads = config["num_attention_pool_heads"]
        self.activation = activation
        self.conv1r = torch.nn.Conv2d(c_in, c_out, kernel_size=3, padding="same", bias=False)
        self.conv1g = torch.nn.Conv2d(c_in, c_gpool, kernel_size=3, padding="same", bias=False)
        self.conv1k = torch.nn.Conv2d(c_in, c_gpool, kernel_size=1, padding="same", bias=False)
        self.conv1q = torch.nn.Conv2d(c_in, c_gpool, kernel_size=1, padding="same", bias=False)

        assert c_gpool % self.c_apheads == 0, "Gpool channels must be divisible by num_attention_pool_heads"

        self.normg = NormMask(
            c_gpool,
            config=config,
            fixup_use_gamma=False,
        )
        self.actg = act(activation, inplace=True)
        self.conv_mix = torch.nn.Conv2d(c_gpool * 2, c_out, kernel_size=1, padding="same", bias=False)

    def initialize(self, scale):
        # Scaling so that variance on the r and g branches adds up to 1.0
        r_scale = 0.8
        g_scale = 0.6
        if self.norm_kind == "fixup" or self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
            init_weights(self.conv1r.weight, self.activation, scale=scale * r_scale)
            init_weights(self.conv1g.weight, self.activation, scale=math.sqrt(scale) * math.sqrt(g_scale))
            init_weights(self.conv1k.weight, "identity", scale=math.sqrt(2.0))
            init_weights(self.conv1q.weight, "identity", scale=math.sqrt(2.0))
            init_weights(self.conv_mix.weight, self.activation, scale=math.sqrt(scale) * math.sqrt(g_scale))
        else:
            init_weights(self.conv1r.weight, self.activation, scale=scale * r_scale)
            init_weights(self.conv1g.weight, self.activation, scale=math.sqrt(scale) * 1.0)
            init_weights(self.conv1k.weight, "identity", scale=math.sqrt(2.0))
            init_weights(self.conv1q.weight, "identity", scale=math.sqrt(2.0))
            init_weights(self.conv_mix.weight, self.activation, scale=math.sqrt(scale) * g_scale)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        reg_dict["normal"].append(self.conv1r.weight)
        reg_dict["normal"].append(self.conv1g.weight)
        reg_dict["output"].append(self.conv1k.weight)
        reg_dict["output"].append(self.conv1q.weight)
        self.normg.add_reg_dict(reg_dict)
        reg_dict["normal"].append(self.conv_mix.weight)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.normg.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.normg.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        n = x.shape[0]
        h = x.shape[2]
        w = x.shape[3]

        out = x
        outr = self.conv1r(out)
        outg = self.conv1g(out)
        outk = self.conv1k(out).view(n * self.c_apheads, self.c_gpool // self.c_apheads, h * w)
        outq = self.conv1q(out).view(n * self.c_apheads, self.c_gpool // self.c_apheads, h * w)
        attention_logits = torch.bmm(torch.transpose(outk, 1, 2), outq)  # n*heads, src h*w, dst h*w
        attention_logits = attention_logits.view(n, self.c_apheads, h * w, h * w)
        attention_logits = attention_logits - (1.0 - mask.view(n, 1, h * w, 1)) * 6000.0
        attention_logits = attention_logits.view(n * self.c_apheads, h * w, h * w)
        attention = torch.nn.functional.softmax(attention_logits, dim=1)
        attention_scale = 0.1 / torch.sqrt(torch.sum(torch.square(attention), dim=1, keepdim=True))  # n*heads, 1, h*w

        outg = self.normg(outg, mask=mask, mask_sum=mask_sum)
        outg = self.actg(outg).view(n * self.c_apheads, self.c_gpool // self.c_apheads, h * w)

        out_pool1 = torch.bmm(outg, attention)
        out_pool2 = out_pool1 * attention_scale
        out_pool1 = out_pool1.view(n, self.c_gpool, h * w)
        out_pool2 = out_pool2.view(n, self.c_gpool, h * w)

        outg = torch.cat((out_pool1, out_pool2), dim=1).view(n, 2 * self.c_gpool, h, w) * mask
        outg = self.conv_mix(outg)
        out = outr + outg
        return out

    # def forward(self, x, mask, mask_sum_hw, mask_sum:float):
    #     """
    #     Parameters:
    #     x: NCHW
    #     mask: N1HW
    #     mask_sum_hw: N111
    #     mask_sum: scalar

    #     Returns: NCHW
    #     """
    #     n = x.shape[0]
    #     h = x.shape[2]
    #     w = x.shape[3]

    #     out = x
    #     outr = self.conv1r(out)
    #     outg = self.conv1g(out)
    #     outk = self.conv1k(out) - (1.0 - mask) * 5000.0
    #     outq = self.conv1q(out)

    #     outk = outk.view(n*self.c_apheads, self.c_gpool//self.c_apheads, h*w)
    #     outq = outq.view(n*self.c_apheads, self.c_gpool//self.c_apheads, h*w)

    #     mask_sum_hw_sqrt_offset = torch.sqrt(mask_sum_hw) - 14.0

    #     outg = self.normg(outg, mask=mask, mask_sum=mask_sum)
    #     outg = self.actg(outg).view(n*self.c_apheads, self.c_gpool//self.c_apheads, h*w)
    #     # Shen et al. Efficient Attention: Attention with Linear Complexities
    #     outg = torch.bmm(outg, torch.transpose(torch.nn.functional.softmax(outk,dim=2),1,2))
    #     outg = torch.bmm(outg, torch.nn.functional.softmax(outq,dim=1))
    #     outg = outg.view(n, self.c_gpool, h, w) * mask
    #     outg = torch.cat((
    #         outg,
    #         outg * (mask_sum_hw_sqrt_offset / 10.0)
    #     ),dim=1)

    #     outg = self.conv_mix(outg)
    #     out = outr + outg
    #     return out


class NormActConv(torch.nn.Module):
    def __init__(
            self,
            c_in: int,
            c_out: int,
            c_gpool: Optional[int],
            config: modelconfigs.ModelConfig,
            activation: str,
            kernel_size: int,
            fixup_use_gamma: bool,
    ):
        super(NormActConv, self).__init__()
        self.c_in = c_in
        self.c_out = c_out
        self.c_gpool = c_gpool
        self.norm = NormMask(
            c_in,
            config=config,
            fixup_use_gamma=fixup_use_gamma,
        )
        self.activation = activation
        self.act = act(activation, inplace=True)
        self.use_repvgg_init = kernel_size > 1 and "use_repvgg_init" in config and config["use_repvgg_init"]

        if c_gpool is not None:
            if config["use_attention_pool"]:
                self.convpool = KataConvAndAttentionPool(c_in=c_in, c_out=c_out, c_gpool=c_gpool, config=config,
                                                         activation=activation)
                self.conv = None
            else:
                self.convpool = KataConvAndGPool(c_in=c_in, c_out=c_out, c_gpool=c_gpool, config=config,
                                                 activation=activation)
                self.conv = None
        else:
            self.conv = torch.nn.Conv2d(c_in, c_out, kernel_size=kernel_size, padding="same", bias=False)
            self.convpool = None

        self.conv1x1 = None
        if self.conv is not None and kernel_size > 1 and "use_repvgg_linear" in config and config["use_repvgg_linear"]:
            self.conv1x1 = torch.nn.Conv2d(c_in, c_out, kernel_size=1, padding="same", bias=False)

    def initialize(self, scale, norm_scale=None):
        self.norm.set_scale(norm_scale)
        if self.convpool is not None:
            self.convpool.initialize(scale=scale)
        else:
            if self.conv1x1 is not None:
                init_weights(self.conv1x1.weight, self.activation, scale=scale * 0.6)
                init_weights(self.conv.weight, self.activation, scale=scale * 0.8)
            else:
                if self.use_repvgg_init:
                    init_weights(self.conv.weight, self.activation, scale=scale * 0.8)
                    center_bonus = self.conv.weight.new_zeros((self.conv.weight.shape[0], self.conv.weight.shape[1]),
                                                              requires_grad=False)
                    init_weights(center_bonus, self.activation, scale=scale * 0.6)
                    self.conv.weight[:, :, 1, 1] += center_bonus
                else:
                    init_weights(self.conv.weight, self.activation, scale=scale)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        self.norm.add_reg_dict(reg_dict)
        if self.convpool is not None:
            self.convpool.add_reg_dict(reg_dict)
        else:
            if self.conv1x1 is not None:
                reg_dict["normal"].append(self.conv1x1.weight)
            reg_dict["normal"].append(self.conv.weight)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.norm.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        if self.convpool is not None:
            self.convpool.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.norm.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        if self.convpool is not None:
            self.convpool.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    @static_vars(counter=0)
    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        NormActConv.forward.counter += 1
        counter = NormActConv.forward.counter
        out = x
        out = self.norm(out, mask=mask, mask_sum=mask_sum)
        Model.dump_tensor(out, 'compare_nnue_output/NormActConv_norm_out_' + str(counter), 'w')
        out = self.act(out)
        Model.dump_tensor(out, 'compare_nnue_output/NormActConv_act_out_' + str(counter), 'w')
        # print("TENSOR AFTER NORMACT")
        # print(out)
        if self.convpool is not None:
            out = self.convpool(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
            Model.dump_tensor(out, 'compare_nnue_output/NormActConv_convpool_out_' + str(counter), 'w')
        else:
            if self.conv1x1 is not None:
                out = self.conv(out) + self.conv1x1(out)
            else:
                out = self.conv(out)
                Model.dump_tensor(out, 'compare_nnue_output/NormActConv_conv_out_' + str(counter), 'w')
        return out


class ResBlock(torch.nn.Module):
    def __init__(
            self,
            name: str,
            c_main: int,
            c_mid: int,
            c_gpool: Optional[int],
            config: modelconfigs.ModelConfig,
            activation: str,
    ):
        super(ResBlock, self).__init__()
        self.name = name
        self.norm_kind = config["norm_kind"]
        self.normactconv1 = NormActConv(
            c_in=c_main,
            c_out=c_mid - (0 if c_gpool is None else c_gpool),
            c_gpool=c_gpool,
            config=config,
            activation=activation,
            kernel_size=3,
            fixup_use_gamma=False,
        )
        self.normactconv2 = NormActConv(
            c_in=c_mid - (0 if c_gpool is None else c_gpool),
            c_out=c_main,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=3,
            fixup_use_gamma=True,
        )

    def initialize(self, fixup_scale):
        if self.norm_kind == "fixup":
            self.normactconv1.initialize(scale=fixup_scale)
            self.normactconv2.initialize(scale=0.0)
        elif self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
            self.normactconv1.initialize(scale=1.0, norm_scale=fixup_scale)
            self.normactconv2.initialize(scale=1.0)
        else:
            self.normactconv1.initialize(scale=1.0)
            self.normactconv2.initialize(scale=1.0)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        self.normactconv1.add_reg_dict(reg_dict)
        self.normactconv2.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.normactconv1.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.normactconv2.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.normactconv1.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.normactconv2.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    @static_vars(counter=0)
    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        ResBlock.forward.counter += 1
        counter = ResBlock.forward.counter
        out = x
        out = self.normactconv1(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        out = self.normactconv2(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        Model.dump_tensor(x + out, 'compare_nnue_output/ResBlock_out_' + str(counter), 'w')
        return x + out


class BottleneckResBlock(torch.nn.Module):
    def __init__(
            self,
            name: str,
            internal_length: int,
            c_main: int,
            c_mid: int,
            c_gpool: Optional[int],
            config: modelconfigs.ModelConfig,
            activation: str,
    ):
        super(BottleneckResBlock, self).__init__()
        self.name = name
        self.norm_kind = config["norm_kind"]
        self.internal_length = internal_length
        assert internal_length >= 1

        self.normactconvp = NormActConv(
            c_in=c_main,
            c_out=c_mid,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=1,
            fixup_use_gamma=False,
        )

        self.normactconvstack = torch.nn.ModuleList()
        self.normactconvstack.append(NormActConv(
            c_in=c_mid,
            c_out=c_mid - (0 if c_gpool is None else c_gpool),
            c_gpool=c_gpool,
            config=config,
            activation=activation,
            kernel_size=3,
            fixup_use_gamma=False,
        ))
        for i in range(self.internal_length - 1):
            self.normactconvstack.append(NormActConv(
                c_in=self.normactconvstack[-1].c_out,
                c_out=c_mid,
                c_gpool=None,
                config=config,
                activation=activation,
                kernel_size=3,
                fixup_use_gamma=False,
            ))

        self.normactconvq = NormActConv(
            c_in=self.normactconvstack[-1].c_out,
            c_out=c_main,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=1,
            fixup_use_gamma=True,
        )

    def initialize(self, fixup_scale):
        if self.norm_kind == "fixup":
            self.normactconvp.initialize(scale=math.pow(fixup_scale, 1.0 / (1.0 + self.internal_length)))
            for i in range(self.internal_length):
                self.normactconvstack[i].initialize(scale=math.pow(fixup_scale, 1.0 / (1.0 + self.internal_length)))
            self.normactconvq.initialize(scale=0.0)
        elif self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
            self.normactconvp.initialize(scale=1.0, norm_scale=fixup_scale)
            for i in range(self.internal_length):
                self.normactconvstack[i].initialize(scale=1.0)
            self.normactconvq.initialize(scale=1.0)
        else:
            self.normactconvp.initialize(scale=1.0)
            for i in range(self.internal_length):
                self.normactconvstack[i].initialize(scale=1.0)
            self.normactconvq.initialize(scale=1.0)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        self.normactconvp.add_reg_dict(reg_dict)
        for i in range(self.internal_length):
            self.normactconvstack[i].add_reg_dict(reg_dict)
        self.normactconvq.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.normactconvp.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        for i in range(self.internal_length):
            self.normactconvstack[i].set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.normactconvq.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.normactconvp.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        for i in range(self.internal_length):
            self.normactconvstack[i].add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.normactconvq.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        out = x
        out = self.normactconvp(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        for i in range(self.internal_length):
            out = self.normactconvstack[i](out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        out = self.normactconvq(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        return x + out


class NestedBottleneckResBlock(torch.nn.Module):
    def __init__(
            self,
            name: str,
            internal_length: int,
            c_main: int,
            c_mid: int,
            c_gpool: Optional[int],
            config: modelconfigs.ModelConfig,
            activation: str,
    ):
        super(NestedBottleneckResBlock, self).__init__()
        self.name = name
        self.norm_kind = config["norm_kind"]
        self.internal_length = internal_length
        assert internal_length >= 1

        self.normactconvp = NormActConv(
            c_in=c_main,
            c_out=c_mid,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=1,
            fixup_use_gamma=False,
        )

        self.blockstack = torch.nn.ModuleList()
        for i in range(self.internal_length):
            self.blockstack.append(ResBlock(
                name=name + "-sub" + str(i),
                c_main=c_mid,
                c_mid=c_mid,
                c_gpool=(c_gpool if i == 0 else None),
                config=config,
                activation=activation,
            ))

        self.normactconvq = NormActConv(
            c_in=c_mid,
            c_out=c_main,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=1,
            fixup_use_gamma=True,
        )

    def initialize(self, fixup_scale):
        if self.norm_kind == "fixup":
            self.normactconvp.initialize(scale=math.pow(fixup_scale, 1.0 / (1.0 + self.internal_length)))
            for i in range(self.internal_length):
                self.blockstack[i].initialize(fixup_scale=math.pow(fixup_scale, 1.0 / (1.0 + self.internal_length)))
            self.normactconvq.initialize(scale=0.0)
        elif self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
            self.normactconvp.initialize(scale=1.0, norm_scale=fixup_scale)
            for i in range(self.internal_length):
                self.blockstack[i].initialize(fixup_scale=1.0 / math.sqrt(i + 1.0))
            self.normactconvq.initialize(scale=1.0, norm_scale=1.0 / math.sqrt(self.internal_length + 1.0))
        else:
            self.normactconvp.initialize(scale=1.0)
            for i in range(self.internal_length):
                self.blockstack[i].initialize(fixup_scale=1.0)
            self.normactconvq.initialize(scale=1.0)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        self.normactconvp.add_reg_dict(reg_dict)
        for i in range(self.internal_length):
            self.blockstack[i].add_reg_dict(reg_dict)
        self.normactconvq.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.normactconvp.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        for i in range(self.internal_length):
            self.blockstack[i].set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.normactconvq.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.normactconvp.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        for i in range(self.internal_length):
            self.blockstack[i].add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.normactconvq.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        out = x
        out = self.normactconvp(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        for i in range(self.internal_length):
            out = self.blockstack[i](out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        out = self.normactconvq(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        return x + out


class NestedNestedBottleneckResBlock(torch.nn.Module):
    def __init__(
            self,
            name: str,
            internal_length: int,
            sub_internal_length: int,
            c_main: int,
            c_outermid: int,
            c_mid: int,
            c_gpool: Optional[int],
            config: modelconfigs.ModelConfig,
            activation: str,
    ):
        super(NestedNestedBottleneckResBlock, self).__init__()
        self.name = name
        self.norm_kind = config["norm_kind"]
        self.internal_length = internal_length
        assert internal_length >= 1

        self.normactconvp = NormActConv(
            c_in=c_main,
            c_out=c_outermid,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=1,
            fixup_use_gamma=False,
        )

        self.blockstack = torch.nn.ModuleList()
        for i in range(self.internal_length):
            self.blockstack.append(NestedBottleneckResBlock(
                name=name + "-sub" + str(i),
                internal_length=sub_internal_length,
                c_main=c_outermid,
                c_mid=c_mid,
                c_gpool=(c_gpool if i == 0 else None),
                config=config,
                activation=activation,
            ))

        self.normactconvq = NormActConv(
            c_in=c_outermid,
            c_out=c_main,
            c_gpool=None,
            config=config,
            activation=activation,
            kernel_size=1,
            fixup_use_gamma=True,
        )

    def initialize(self, fixup_scale):
        if self.norm_kind == "fixup":
            self.normactconvp.initialize(scale=math.pow(fixup_scale, 1.0 / (1.0 + self.internal_length)))
            for i in range(self.internal_length):
                self.blockstack[i].initialize(fixup_scale=math.pow(fixup_scale, 1.0 / (1.0 + self.internal_length)))
            self.normactconvq.initialize(scale=0.0)
        elif self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
            self.normactconvp.initialize(scale=1.0, norm_scale=fixup_scale)
            for i in range(self.internal_length):
                self.blockstack[i].initialize(fixup_scale=1.0 / math.sqrt(i + 1.0))
            self.normactconvq.initialize(scale=1.0, norm_scale=1.0 / math.sqrt(self.internal_length + 1.0))
        else:
            self.normactconvp.initialize(scale=1.0)
            for i in range(self.internal_length):
                self.blockstack[i].initialize(fixup_scale=1.0)
            self.normactconvq.initialize(scale=1.0)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        self.normactconvp.add_reg_dict(reg_dict)
        for i in range(self.internal_length):
            self.blockstack[i].add_reg_dict(reg_dict)
        self.normactconvq.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        self.normactconvp.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        for i in range(self.internal_length):
            self.blockstack[i].set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.normactconvq.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        self.normactconvp.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        for i in range(self.internal_length):
            self.blockstack[i].add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.normactconvq.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        """
        Parameters:
        x: NCHW
        mask: N1HW
        mask_sum_hw: N111
        mask_sum: scalar

        Returns: NCHW
        """
        out = x
        out = self.normactconvp(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        for i in range(self.internal_length):
            out = self.blockstack[i](out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        out = self.normactconvq(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        return x + out


class PolicyHead(torch.nn.Module):
    def __init__(self, c_in, c_p1, c_g1, config, activation):
        super(PolicyHead, self).__init__()
        self.activation = activation

        if config["version"] <= 11:
            self.num_policy_outputs = 4
        else:
            self.num_policy_outputs = 6
        # Output 0: policy prediction
        # Output 1: opponent reply policy prediction
        # Output 2: soft policy prediction
        # Output 3: soft opponent reply policy prediction
        # Output 4: long-term-optimistic policy prediction
        # Output 5: short-term-optimistic policy prediction

        self.conv1p = torch.nn.Conv2d(c_in, c_p1, kernel_size=1, padding="same", bias=False)
        self.conv1g = torch.nn.Conv2d(c_in, c_g1, kernel_size=1, padding="same", bias=False)

        self.biasg = BiasMask(
            c_g1,
            config=config,
            is_after_batchnorm=True,
        )
        self.actg = act(self.activation)
        self.gpool = KataGPool()

        self.linear_g = torch.nn.Linear(3 * c_g1, c_p1, bias=False)
        self.linear_pass = torch.nn.Linear(3 * c_g1, self.num_policy_outputs, bias=False)

        self.bias2 = BiasMask(
            c_p1,
            config=config,
            is_after_batchnorm=True,
        )
        self.act2 = act(activation)
        self.conv2p = torch.nn.Conv2d(c_p1, self.num_policy_outputs, kernel_size=1, padding="same", bias=False)

    def initialize(self):
        # Scaling so that variance on the p and g branches adds up to 1.0
        p_scale = 0.8
        g_scale = 0.6
        # Extra scaling for outputs
        scale_output = 0.3
        init_weights(self.conv1p.weight, self.activation, scale=p_scale)
        init_weights(self.conv1g.weight, self.activation, scale=1.0)
        init_weights(self.linear_g.weight, self.activation, scale=g_scale)
        init_weights(self.linear_pass.weight, "identity", scale=scale_output)
        init_weights(self.conv2p.weight, "identity", scale=scale_output)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        reg_dict["output"].append(self.conv1p.weight)
        reg_dict["output"].append(self.conv1g.weight)
        reg_dict["output"].append(self.linear_g.weight)
        reg_dict["output"].append(self.linear_pass.weight)
        reg_dict["output"].append(self.conv2p.weight)
        self.biasg.add_reg_dict(reg_dict)
        self.bias2.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        pass

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        pass

    def forward(self, x, mask, mask_sum_hw, mask_sum: float):
        outp = self.conv1p(x)
        Model.dump_tensor(outp, 'compare_nnue_output/policy_head_conv1p_out', 'w')
        outg = self.conv1g(x)
        Model.dump_tensor(outg, 'compare_nnue_output/policy_head_conv1g_out', 'w')
        outg = self.biasg(outg, mask=mask, mask_sum=mask_sum)
        outg = self.actg(outg)
        Model.dump_tensor(outg, 'compare_nnue_output/policy_head_relu1_out', 'w')
        outg = self.gpool(outg, mask=mask, mask_sum_hw=mask_sum_hw).squeeze(-1).squeeze(-1)  # NC
        Model.dump_tensor(outg, 'compare_nnue_output/policy_head_gpool_out', 'w')
        outpass = self.linear_pass(outg)  # NC
        outg = self.linear_g(outg).unsqueeze(-1).unsqueeze(-1)  # NCHW
        Model.dump_tensor(outg, 'compare_nnue_output/policy_head_linear_out', 'w')

        outp = outp + outg
        outp = self.bias2(outp, mask=mask, mask_sum=mask_sum)
        outp = self.act2(outp)
        outp = self.conv2p(outp)
        outpolicy = outp

        # mask out parts outside the board by making them a huge neg number, so that they're 0 after softmax
        outpolicy = outpolicy - (1.0 - mask) * 5000.0
        # NC(HW) concat with NC1
        return torch.cat((outpolicy.view(outpolicy.shape[0], outpolicy.shape[1], -1), outpass.unsqueeze(-1)), dim=2)


class ValueHead(torch.nn.Module):
    def __init__(self, c_in, c_v1, c_v2, c_sv2, num_scorebeliefs, config, activation, pos_len):
        super(ValueHead, self).__init__()
        self.activation = activation
        self.conv1 = torch.nn.Conv2d(c_in, c_v1, kernel_size=1, padding="same", bias=False)
        self.bias1 = BiasMask(
            c_v1,
            config=config,
            is_after_batchnorm=True,
        )
        self.act1 = act(activation)
        self.gpool = KataValueHeadGPool()

        self.linear2 = torch.nn.Linear(3 * c_v1, c_v2, bias=True)
        self.act2 = act(activation)

        self.linear_valuehead = torch.nn.Linear(c_v2, 3, bias=True)
        self.linear_miscvaluehead = torch.nn.Linear(c_v2, 10, bias=True)
        self.linear_moremiscvaluehead = torch.nn.Linear(c_v2, 8, bias=True)
        self.conv_ownership = torch.nn.Conv2d(c_v1, 1, kernel_size=1, padding="same", bias=False)
        self.conv_scoring = torch.nn.Conv2d(c_v1, 1, kernel_size=1, padding="same", bias=False)
        self.conv_futurepos = torch.nn.Conv2d(c_in, 2, kernel_size=1, padding="same", bias=False)
        self.conv_seki = torch.nn.Conv2d(c_in, 4, kernel_size=1, padding="same", bias=False)

        self.pos_len = pos_len
        self.scorebelief_mid = self.pos_len * self.pos_len + EXTRA_SCORE_DISTR_RADIUS
        self.scorebelief_len = self.scorebelief_mid * 2
        self.num_scorebeliefs = num_scorebeliefs
        self.c_sv2 = c_sv2

        self.linear_s2 = torch.nn.Linear(3 * c_v1, c_sv2, bias=True)
        self.linear_s2off = torch.nn.Linear(1, c_sv2, bias=False)
        self.linear_s2par = torch.nn.Linear(1, c_sv2, bias=False)
        self.linear_s3 = torch.nn.Linear(c_sv2, num_scorebeliefs, bias=True)
        self.linear_smix = torch.nn.Linear(3 * c_v1, num_scorebeliefs, bias=True)

        self.register_buffer("score_belief_offset_vector", torch.tensor(
            data=[(float(i - self.scorebelief_mid) + 0.5) for i in range(self.scorebelief_len)],
            dtype=torch.float32,
            requires_grad=False,
        ), persistent=False)
        self.register_buffer("score_belief_offset_bias_vector", torch.tensor(
            data=[0.05 * (float(i - self.scorebelief_mid) + 0.5) for i in range(self.scorebelief_len)],
            dtype=torch.float32,
            requires_grad=False,
        ), persistent=False)
        self.register_buffer("score_belief_parity_vector", torch.tensor(
            [0.5 - float((i - self.scorebelief_mid) % 2) for i in range(self.scorebelief_len)],
            dtype=torch.float32,
            requires_grad=False,
        ), persistent=False)

    def initialize(self):
        bias_scale = 0.2
        init_weights(self.conv1.weight, self.activation, scale=1.0)
        init_weights(self.linear2.weight, self.activation, scale=1.0)
        init_weights(self.linear2.bias, self.activation, scale=bias_scale, fan_tensor=self.linear2.weight)

        init_weights(self.linear_valuehead.weight, "identity", scale=1.0)
        init_weights(self.linear_valuehead.bias, "identity", scale=bias_scale, fan_tensor=self.linear_valuehead.weight)

        init_weights(self.linear_miscvaluehead.weight, "identity", scale=1.0)
        init_weights(self.linear_miscvaluehead.bias, "identity", scale=bias_scale,
                     fan_tensor=self.linear_miscvaluehead.weight)

        init_weights(self.linear_moremiscvaluehead.weight, "identity", scale=1.0)
        init_weights(self.linear_moremiscvaluehead.bias, "identity", scale=bias_scale,
                     fan_tensor=self.linear_moremiscvaluehead.weight)

        aux_spatial_output_scale = 0.2
        init_weights(self.conv_ownership.weight, "identity", scale=aux_spatial_output_scale)
        init_weights(self.conv_scoring.weight, "identity", scale=aux_spatial_output_scale)
        init_weights(self.conv_futurepos.weight, "identity", scale=aux_spatial_output_scale)
        init_weights(self.conv_seki.weight, "identity", scale=aux_spatial_output_scale)

        init_weights(self.linear_s2.weight, self.activation, scale=1.0)
        init_weights(self.linear_s2.bias, self.activation, scale=1.0, fan_tensor=self.linear_s2.weight)
        init_weights(self.linear_s2off.weight, self.activation, scale=1.0, fan_tensor=self.linear_s2.weight)
        init_weights(self.linear_s2par.weight, self.activation, scale=1.0, fan_tensor=self.linear_s2.weight)

        scorebelief_output_scale = 0.5
        init_weights(self.linear_s3.weight, "identity", scale=scorebelief_output_scale)
        init_weights(self.linear_s3.bias, "identity", scale=scorebelief_output_scale * bias_scale,
                     fan_tensor=self.linear_s3.weight)
        init_weights(self.linear_smix.weight, "identity", scale=1.0)
        init_weights(self.linear_smix.bias, "identity", scale=bias_scale, fan_tensor=self.linear_smix.weight)

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        reg_dict["output"].append(self.conv1.weight)
        reg_dict["output"].append(self.linear2.weight)
        reg_dict["output_noreg"].append(self.linear2.bias)
        reg_dict["output"].append(self.linear_valuehead.weight)
        reg_dict["output_noreg"].append(self.linear_valuehead.bias)
        reg_dict["output"].append(self.linear_miscvaluehead.weight)
        reg_dict["output_noreg"].append(self.linear_miscvaluehead.bias)
        reg_dict["output"].append(self.linear_moremiscvaluehead.weight)
        reg_dict["output_noreg"].append(self.linear_moremiscvaluehead.bias)
        reg_dict["output"].append(self.conv_ownership.weight)
        reg_dict["output"].append(self.conv_scoring.weight)
        reg_dict["output"].append(self.conv_futurepos.weight)
        reg_dict["output"].append(self.conv_seki.weight)
        reg_dict["output"].append(self.linear_s2.weight)
        reg_dict["output_noreg"].append(self.linear_s2.bias)
        reg_dict["output"].append(self.linear_s2off.weight)
        reg_dict["output"].append(self.linear_s2par.weight)
        reg_dict["output"].append(self.linear_s3.weight)
        reg_dict["output_noreg"].append(self.linear_s3.bias)
        reg_dict["output"].append(self.linear_smix.weight)
        reg_dict["output_noreg"].append(self.linear_smix.bias)
        self.bias1.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        pass

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        pass

    def forward(self, x, mask, mask_sum_hw, mask_sum: float, input_global):
        outv1 = x
        outv1 = self.conv1(outv1)
        outv1 = self.bias1(outv1, mask=mask, mask_sum=mask_sum)
        outv1 = self.act1(outv1)

        outpooled = self.gpool(outv1, mask=mask, mask_sum_hw=mask_sum_hw).squeeze(-1).squeeze(-1)

        outv2 = self.linear2(outpooled)
        outv2 = self.act2(outv2)

        # Different subheads
        out_value = self.linear_valuehead(outv2)
        out_miscvalue = self.linear_miscvaluehead(outv2)
        out_moremiscvalue = self.linear_moremiscvaluehead(outv2)
        out_ownership = self.conv_ownership(outv1) * mask
        out_scoring = self.conv_scoring(outv1) * mask
        out_futurepos = self.conv_futurepos(x) * mask
        out_seki = self.conv_seki(x) * mask

        # Score belief head
        batch_size = x.shape[0]
        outsv2 = (
                self.linear_s2(outpooled).view(batch_size, 1, self.c_sv2) +
                self.linear_s2off(self.score_belief_offset_bias_vector.view(1, self.scorebelief_len, 1)) +
                self.linear_s2par(
                    (self.score_belief_parity_vector.view(1, self.scorebelief_len) * input_global[:, -1:]).view(
                        batch_size, self.scorebelief_len, 1))
        )  # N,scorebelief_len,c_sv2

        outsv2 = self.act2(outsv2)
        outsv3 = self.linear_s3(outsv2)  # N, scorebelief_len, num_scorebeliefs

        outsmix = self.linear_smix(outpooled)  # N, num_scorebeliefs
        outsmix_logweights = torch.nn.functional.log_softmax(outsmix, dim=1)
        # For each of num_scorebeliefs, compute softmax to make it into probability distribution
        out_scorebelief_logprobs = torch.nn.functional.log_softmax(outsv3, dim=1)
        # Take the mixture distribution weighted by outsmix_weights
        out_scorebelief_logprobs = torch.logsumexp(
            out_scorebelief_logprobs + outsmix_logweights.view(-1, 1, self.num_scorebeliefs), dim=2)

        return (
            out_value,
            out_miscvalue,
            out_moremiscvalue,
            out_ownership,
            out_scoring,
            out_futurepos,
            out_seki,
            out_scorebelief_logprobs,
        )


class Model(torch.nn.Module):
    def __init__(self, config: modelconfigs.ModelConfig, pos_len: int):
        super(Model, self).__init__()

        self.config = config
        self.norm_kind = config["norm_kind"]
        self.block_kind = config["block_kind"]
        self.c_trunk = config["trunk_num_channels"]
        self.c_mid = config["mid_num_channels"]
        self.c_gpool = config["gpool_num_channels"]
        self.c_outermid = config["outermid_num_channels"] if "outermid_num_channels" in config else self.c_mid
        self.c_p1 = config["p1_num_channels"]
        self.c_g1 = config["g1_num_channels"]
        self.c_v1 = config["v1_num_channels"]
        self.c_v2 = config["v2_size"]
        self.c_sv2 = config["sbv2_num_channels"]
        self.num_scorebeliefs = config["num_scorebeliefs"]
        self.num_total_blocks = len(self.block_kind)
        self.pos_len = pos_len

        if config["version"] <= 12:
            self.td_score_multiplier = 20.0
            self.scoremean_multiplier = 20.0
            self.scorestdev_multiplier = 20.0
            self.lead_multiplier = 20.0
            self.variance_time_multiplier = 40.0
            self.shortterm_value_error_multiplier = 0.25
            self.shortterm_score_error_multiplier = 30.0
        else:
            self.td_score_multiplier = 20.0
            self.scoremean_multiplier = 20.0
            self.scorestdev_multiplier = 20.0
            self.lead_multiplier = 20.0
            self.variance_time_multiplier = 40.0
            self.shortterm_value_error_multiplier = 0.25
            self.shortterm_score_error_multiplier = 150.0

        self.trunk_normless = "trunk_normless" in config and config["trunk_normless"]

        if "has_intermediate_head" in config and config["has_intermediate_head"]:
            self.has_intermediate_head = True
            self.intermediate_head_blocks = config["intermediate_head_blocks"]
        else:
            self.has_intermediate_head = False
            self.intermediate_head_blocks = 0

        self.activation = "relu" if "activation" not in config else config["activation"]

        if config["initial_conv_1x1"]:
            self.conv_spatial = torch.nn.Conv2d(22, self.c_trunk, kernel_size=1, padding="same", bias=False)
        else:
            self.conv_spatial = torch.nn.Conv2d(22, self.c_trunk, kernel_size=3, padding="same", bias=False)
        self.linear_global = torch.nn.Linear(19, self.c_trunk, bias=False)

        self.bin_input_shape = [22, pos_len, pos_len]
        self.global_input_shape = [19]

        self.blocks = torch.nn.ModuleList()
        for block_config in self.block_kind:
            block_name = block_config[0]
            block_kind = block_config[1]
            use_gpool_this_block = False
            if block_kind.endswith("gpool"):
                use_gpool_this_block = True
                block_kind = block_kind[:-5]

            if block_kind == "regular":
                self.blocks.append(ResBlock(
                    name=block_name,
                    c_main=self.c_trunk,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            elif block_kind == "bottle1" or block_kind == "bottle":
                self.blocks.append(BottleneckResBlock(
                    name=block_name,
                    internal_length=1,
                    c_main=self.c_trunk,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            elif block_kind == "bottle2":
                self.blocks.append(BottleneckResBlock(
                    name=block_name,
                    internal_length=2,
                    c_main=self.c_trunk,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            elif block_kind == "bottle3":
                self.blocks.append(BottleneckResBlock(
                    name=block_name,
                    internal_length=3,
                    c_main=self.c_trunk,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            elif block_kind == "bottlenest2":
                self.blocks.append(NestedBottleneckResBlock(
                    name=block_name,
                    internal_length=2,
                    c_main=self.c_trunk,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            elif block_kind == "bottlenest3":
                self.blocks.append(NestedBottleneckResBlock(
                    name=block_name,
                    internal_length=3,
                    c_main=self.c_trunk,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            elif block_kind == "bottlenest2bottlenest2":
                self.blocks.append(NestedNestedBottleneckResBlock(
                    name=block_name,
                    internal_length=2,
                    sub_internal_length=2,
                    c_main=self.c_trunk,
                    c_outermid=self.c_outermid,
                    c_mid=self.c_mid,
                    c_gpool=(self.c_gpool if use_gpool_this_block else None),
                    config=self.config,
                    activation=self.activation,
                ))
            else:
                assert False, f"Unknown block kind: {block_config[1]}"

        if self.trunk_normless:
            self.norm_trunkfinal = BiasMask(self.c_trunk, self.config, is_after_batchnorm=True)
        else:
            self.norm_trunkfinal = NormMask(self.c_trunk, self.config, fixup_use_gamma=False, is_last_batchnorm=True)
        self.act_trunkfinal = act(self.activation)

        self.policy_head = PolicyHead(
            self.c_trunk,
            self.c_p1,
            self.c_g1,
            self.config,
            self.activation,
        )
        self.value_head = ValueHead(
            self.c_trunk,
            self.c_v1,
            self.c_v2,
            self.c_sv2,
            self.num_scorebeliefs,
            self.config,
            self.activation,
            self.pos_len,
        )
        if self.has_intermediate_head:
            self.norm_intermediate_trunkfinal = NormMask(self.c_trunk, self.config, fixup_use_gamma=False,
                                                         is_last_batchnorm=True)
            self.act_intermediate_trunkfinal = act(self.activation)
            self.intermediate_policy_head = PolicyHead(
                self.c_trunk,
                self.c_p1,
                self.c_g1,
                self.config,
                self.activation,
            )
            self.intermediate_value_head = ValueHead(
                self.c_trunk,
                self.c_v1,
                self.c_v2,
                self.c_sv2,
                self.num_scorebeliefs,
                self.config,
                self.activation,
                self.pos_len,
            )

    def initialize(self):
        with torch.no_grad():
            spatial_scale = 0.8
            global_scale = 0.6
            init_weights(self.conv_spatial.weight, self.activation, scale=spatial_scale)
            init_weights(self.linear_global.weight, self.activation, scale=global_scale)

            if self.norm_kind == "fixup":
                fixup_scale = 1.0 / math.sqrt(self.num_total_blocks)
                for block in self.blocks:
                    block.initialize(fixup_scale=fixup_scale)
            elif self.norm_kind == "fixscale" or self.norm_kind == "fixbrenorm" or self.norm_kind == "fixscaleonenorm":
                for i, block in enumerate(self.blocks):
                    block.initialize(fixup_scale=1.0 / math.sqrt(i + 1.0))
                self.norm_trunkfinal.set_scale(1.0 / math.sqrt(self.num_total_blocks + 1.0))
            else:
                for block in self.blocks:
                    block.initialize(fixup_scale=1.0)

            self.policy_head.initialize()
            self.value_head.initialize()
            if self.has_intermediate_head:
                self.intermediate_policy_head.initialize()
                self.intermediate_value_head.initialize()

    def get_norm_kind(self) -> bool:
        return self.norm_kind

    def get_has_intermediate_head(self) -> bool:
        return self.has_intermediate_head

    def add_reg_dict(self, reg_dict: Dict[str, List]):
        reg_dict["normal"] = []
        reg_dict["normal_gamma"] = []
        reg_dict["output"] = []
        reg_dict["noreg"] = []
        reg_dict["output_noreg"] = []

        reg_dict["normal"].append(self.conv_spatial.weight)
        reg_dict["normal"].append(self.linear_global.weight)
        for block in self.blocks:
            block.add_reg_dict(reg_dict)
        self.norm_trunkfinal.add_reg_dict(reg_dict)
        self.policy_head.add_reg_dict(reg_dict)
        self.value_head.add_reg_dict(reg_dict)
        if self.has_intermediate_head:
            self.norm_intermediate_trunkfinal.add_reg_dict(reg_dict)
            self.intermediate_policy_head.add_reg_dict(reg_dict)
            self.intermediate_value_head.add_reg_dict(reg_dict)

    def set_brenorm_params(self, renorm_avg_momentum: float, rmax: float, dmax: float):
        for block in self.blocks:
            block.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.norm_trunkfinal.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.policy_head.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        self.value_head.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
        if self.has_intermediate_head:
            self.norm_intermediate_trunkfinal.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
            self.intermediate_policy_head.set_brenorm_params(renorm_avg_momentum, rmax, dmax)
            self.intermediate_value_head.set_brenorm_params(renorm_avg_momentum, rmax, dmax)

    def add_brenorm_clippage(self, upper_rclippage, lower_rclippage, dclippage):
        for block in self.blocks:
            block.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.norm_trunkfinal.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.policy_head.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        self.value_head.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
        if self.has_intermediate_head:
            self.norm_intermediate_trunkfinal.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
            self.intermediate_policy_head.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)
            self.intermediate_value_head.add_brenorm_clippage(upper_rclippage, lower_rclippage, dclippage)

    # Returns a tuple of tuples of outputs
    # The outer tuple indexes different sets of heads, such as if the net also computes intermediate heads.
    #   0 is the main output, 1 is intermediate.
    # The inner tuple ranges over the outputs of a set of heads (policy, value, etc).
    def forward(self, input_spatial, input_global):
        # float_formatter = "{:.3f}".format
        # np.set_printoptions(formatter={'float_kind':float_formatter}, threshold=1000000, linewidth=10000)

        mask = input_spatial[:, 0:1, :, :].contiguous()
        mask_sum_hw = torch.sum(mask, dim=(2, 3), keepdim=True)
        mask_sum = torch.sum(mask)

        x_spatial = self.conv_spatial(input_spatial)
        Model.dump_tensor(x_spatial.data, 'compare_nnue_output/spatial_tensor_out', 'w')
        x_global = self.linear_global(input_global).unsqueeze(-1).unsqueeze(-1)
        out = x_spatial + x_global
        # print("TENSOR BEFORE TRUNK")
        # print(out)

        if self.has_intermediate_head:
            count = 0
            for block in self.blocks[:self.intermediate_head_blocks]:
                # print("TENSOR BEFORE BLOCK")
                # print(count)
                # print(out)
                out = block(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
                count += 1

            # print("INTERMEDIATE")
            iout = out
            iout = self.norm_intermediate_trunkfinal(iout, mask=mask, mask_sum=mask_sum)
            iout = self.act_intermediate_trunkfinal(iout)
            iout_policy = self.intermediate_policy_head(iout, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
            (
                iout_value,
                iout_miscvalue,
                iout_moremiscvalue,
                iout_ownership,
                iout_scoring,
                iout_futurepos,
                iout_seki,
                iout_scorebelief_logprobs,
            ) = self.intermediate_value_head(iout, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum,
                                             input_global=input_global)

            for block in self.blocks[self.intermediate_head_blocks:]:
                # print("TENSOR BEFORE BLOCK")
                # print(count)
                # print(out)
                out = block(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
                count += 1

        else:
            count = 0
            for block in self.blocks:
                # print("TENSOR BEFORE BLOCK")
                # print(count)
                # print(out)
                out = block(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
                count += 1

        Model.dump_tensor(out, 'compare_nnue_output/res_blocks_out', 'w')
        out = self.norm_trunkfinal(out, mask=mask, mask_sum=mask_sum)
        out = self.act_trunkfinal(out)
        Model.dump_tensor(out, 'compare_nnue_output/res_blocks_out_final', 'w')

        # print("MAIN")
        out_policy = self.policy_head(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum)
        (
            out_value,
            out_miscvalue,
            out_moremiscvalue,
            out_ownership,
            out_scoring,
            out_futurepos,
            out_seki,
            out_scorebelief_logprobs,
        ) = self.value_head(out, mask=mask, mask_sum_hw=mask_sum_hw, mask_sum=mask_sum, input_global=input_global)

        if self.has_intermediate_head:
            return (
                (
                    out_policy,
                    out_value,
                    out_miscvalue,
                    out_moremiscvalue,
                    out_ownership,
                    out_scoring,
                    out_futurepos,
                    out_seki,
                    out_scorebelief_logprobs,
                ),
                (
                    iout_policy,
                    iout_value,
                    iout_miscvalue,
                    iout_moremiscvalue,
                    iout_ownership,
                    iout_scoring,
                    iout_futurepos,
                    iout_seki,
                    iout_scorebelief_logprobs,
                ),
            )
        else:
            return ((
                        out_policy,
                        out_value,
                        out_miscvalue,
                        out_moremiscvalue,
                        out_ownership,
                        out_scoring,
                        out_futurepos,
                        out_seki,
                        out_scorebelief_logprobs,
                    ),)

    def float32ify_output(self, outputs_byheads):
        return tuple(self.float32ify_single_heads_output(outputs) for outputs in outputs_byheads)

    def float32ify_single_heads_output(self, outputs):
        (
            out_policy,
            out_value,
            out_miscvalue,
            out_moremiscvalue,
            out_ownership,
            out_scoring,
            out_futurepos,
            out_seki,
            out_scorebelief_logprobs,
        ) = outputs
        return (
            out_policy.to(torch.float32),
            out_value.to(torch.float32),
            out_miscvalue.to(torch.float32),
            out_moremiscvalue.to(torch.float32),
            out_ownership.to(torch.float32),
            out_scoring.to(torch.float32),
            out_futurepos.to(torch.float32),
            out_seki.to(torch.float32),
            out_scorebelief_logprobs.to(torch.float32),
        )

    def postprocess_output(self, outputs_byheads):
        return tuple(self.postprocess_single_heads_output(outputs) for outputs in outputs_byheads)

    def postprocess_single_heads_output(self, outputs):
        (
            out_policy,
            out_value,
            out_miscvalue,
            out_moremiscvalue,
            out_ownership,
            out_scoring,
            out_futurepos,
            out_seki,
            out_scorebelief_logprobs,
        ) = outputs

        policy_logits = out_policy
        value_logits = out_value
        td_value_logits = torch.stack((out_miscvalue[:, 4:7], out_miscvalue[:, 7:10], out_moremiscvalue[:, 2:5]), dim=1)
        pred_td_score = out_moremiscvalue[:, 5:8] * self.td_score_multiplier
        ownership_pretanh = out_ownership
        pred_scoring = out_scoring
        futurepos_pretanh = out_futurepos
        seki_logits = out_seki
        pred_scoremean = out_miscvalue[:, 0] * self.scoremean_multiplier
        pred_scorestdev = SoftPlusWithGradientFloorFunction.apply(out_miscvalue[:, 1], 0.05,
                                                                  False) * self.scorestdev_multiplier
        pred_lead = out_miscvalue[:, 2] * self.lead_multiplier
        pred_variance_time = SoftPlusWithGradientFloorFunction.apply(out_miscvalue[:, 3], 0.05,
                                                                     False) * self.variance_time_multiplier
        if self.config["version"] < 14:
            pred_shortterm_value_error = SoftPlusWithGradientFloorFunction.apply(out_moremiscvalue[:, 0], 0.05,
                                                                                 False) * self.shortterm_value_error_multiplier
            pred_shortterm_score_error = SoftPlusWithGradientFloorFunction.apply(out_moremiscvalue[:, 1], 0.05,
                                                                                 False) * self.shortterm_score_error_multiplier
        else:
            pred_shortterm_value_error = SoftPlusWithGradientFloorFunction.apply(out_moremiscvalue[:, 0], 0.05,
                                                                                 True) * self.shortterm_value_error_multiplier
            pred_shortterm_score_error = SoftPlusWithGradientFloorFunction.apply(out_moremiscvalue[:, 1], 0.05,
                                                                                 True) * self.shortterm_score_error_multiplier
        scorebelief_logits = out_scorebelief_logprobs

        return (
            policy_logits,  # N, num_policy_outputs, move
            value_logits,  # N, {win,loss,noresult}
            td_value_logits,  # N, {long, mid, short} {win,loss,noresult}
            pred_td_score,  # N, {long, mid, short}
            ownership_pretanh,  # N, 1, y, x
            pred_scoring,  # N, 1, y, x
            futurepos_pretanh,  # N, 2, y, x
            seki_logits,  # N, 4, y, x
            pred_scoremean,  # N
            pred_scorestdev,  # N
            pred_lead,  # N
            pred_variance_time,  # N
            pred_shortterm_value_error,  # N
            pred_shortterm_score_error,  # N
            scorebelief_logits,  # N, 2 * (self.pos_len*self.pos_len + EXTRA_SCORE_DISTR_RADIUS)
        )

    @staticmethod
    @static_vars(dump=False)
    def dump_tensor(w, path_end, mode):
        if not Model.dump_tensor.dump:
            return
        test_folder = ''
        if mode == 'w':
            test_folder = get_test_data_folder()
        file = open(test_folder + path_end, mode)
        weights = w.detach().cpu()
        shape = weights.shape
        for x in np.ndindex(shape):
            v = weights[x].item()
            file.write(str(v) + " ")
        file.write("\n")

    @staticmethod
    def dump_weights(swa_model, out_file_nnue_weights_path):
        sw_ref = swa_model.module.conv_spatial.weight
        spatial_conv_weights = sw_ref.data[:, :3, :, :]  # second coordinate will be :2 in the long run
        conv1_weights = swa_model.module.blocks[0].normactconv1.conv.weight
        conv2_weights = swa_model.module.blocks[0].normactconv2.conv.weight
        policy_head = swa_model.module.policy_head
        Model.dump_tensor(spatial_conv_weights, out_file_nnue_weights_path, 'a')
        Model.dump_tensor(conv1_weights, out_file_nnue_weights_path, 'a')
        Model.dump_tensor(conv2_weights, out_file_nnue_weights_path, 'a')
        Model.dump_tensor(policy_head.conv1p.weight, out_file_nnue_weights_path, 'a')
        Model.dump_tensor(policy_head.conv1g.weight, out_file_nnue_weights_path, 'a')
        Model.dump_tensor(policy_head.linear_g.weight, out_file_nnue_weights_path, 'a')
